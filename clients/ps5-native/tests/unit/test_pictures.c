#include <stdlib.h>

#include "akeno/json.h"
#include "badges.h"
#include "fixtures_jpeg.h"
#include "image.h"
#include "png.h"
#include "social.h"
#include "test.h"
#include "vector.h"

static int near(const uint8_t *px, int r, int g, int b, int tol) {
    return abs(px[0] - r) <= tol && abs(px[1] - g) <= tol && abs(px[2] - b) <= tol;
}

static void test_base64_and_ids(void) {
    uint8_t out[16];
    CHECK(akn_base64_decode("aGVsbG8=", 8, out, sizeof(out)) == 5 && memcmp(out, "hello", 5) == 0);
    CHECK(akn_base64_decode("aGVsbG8", 7, out, sizeof(out)) == 5 && memcmp(out, "hello", 5) == 0);
    CHECK(akn_base64_decode("+/8=", 4, out, sizeof(out)) == 2 && out[0] == 0xFB && out[1] == 0xFF);
    CHECK(akn_base64_decode("", 0, out, sizeof(out)) == 0);
    CHECK(akn_base64_decode("aGV sbG8=", 9, out, sizeof(out)) == -1); /* whitespace */
    CHECK(akn_base64_decode("aGVs*G8=", 8, out, sizeof(out)) == -1);
    CHECK(akn_base64_decode("a", 1, out, sizeof(out)) == -1);         /* one char cannot be a byte */
    CHECK(akn_base64_decode("aGVsbG8=", 8, out, 4) == -1);           /* too small: nothing written past it */
    CHECK(akn_base64_decode("aG=Vs", 5, out, sizeof(out)) == -1);     /* padding in the middle */

    CHECK(akn_is_media_id("0123456789abcdef0123456789abcdef"));
    CHECK(!akn_is_media_id("0123456789ABCDEF0123456789abcdef"));
    CHECK(!akn_is_media_id("0123456789abcdef0123456789abcde"));
    CHECK(!akn_is_media_id("0123456789abcdef0123456789abcdef0"));
    CHECK(!akn_is_media_id("../../../../etc/passwd&v=lg&id=00"));
    CHECK(!akn_is_media_id("") && !akn_is_media_id(NULL));
    CHECK(akn_is_title_id("PPSA01234") && akn_is_title_id("CUSA00001"));
    CHECK(!akn_is_title_id("ppsa01234") && !akn_is_title_id("PPSA0123") && !akn_is_title_id("PPSA012345") && !akn_is_title_id("PP/A01234"));
    CHECK(!akn_is_title_id(NULL));
}

static void test_decode(void) {
    akn_image img;
    char err[96];
    CHECK(akn_image_decode(JPEG_BASELINE, sizeof(JPEG_BASELINE), &img, err, sizeof(err)) == 0);
    CHECK(img.w == 16 && img.h == 8 && img.rgba);
    if (img.rgba) CHECK(near(img.rgba + (3 * 16 + 5) * 4, 200, 40, 90, 6) && img.rgba[(3 * 16 + 5) * 4 + 3] == 255);
    akn_image_free(&img);
    CHECK(!img.rgba);
    CHECK(akn_image_decode(JPEG_PROGRESSIVE, sizeof(JPEG_PROGRESSIVE), &img, err, sizeof(err)) == 0);
    CHECK(img.w == 8 && img.h == 8);
    if (img.rgba) CHECK(near(img.rgba + 20 * 4, 30, 160, 220, 6));
    akn_image_free(&img);

    /* PNG with transparency, round trip through the app's own encoder */
    uint8_t px[3 * 2 * 4] = { 255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 1 };
    uint8_t *png = NULL;
    size_t len = 0;
    CHECK(akn_png_encode(px, 3, 2, 12, &png, &len) == 0);
    CHECK(akn_image_decode(png, len, &img, err, sizeof(err)) == 0);
    CHECK(img.w == 3 && img.h == 2 && img.rgba && memcmp(img.rgba, px, sizeof(px)) == 0);
    akn_image_free(&img);

    /* an empty first image-data chunk (found by fuzzing): refused before stb_image sees it */
    uint8_t *odd = malloc(len + 12);
    size_t at = 8 + 25; /* after the signature and IHDR */
    memcpy(odd, png, at);
    memcpy(odd + at, "\0\0\0\0IDAT\x35\xAF\x06\x1E", 12);
    memcpy(odd + at + 12, png + at, len - at);
    CHECK(akn_image_decode(odd, len + 12, &img, err, sizeof(err)) == -1 && !img.rgba);
    CHECK_STR(err, "damaged PNG");
    free(odd);

    /* broken input: an error, nothing allocated */
    memset(&img, 0x55, sizeof(img));
    CHECK(akn_image_decode(png, len / 2, &img, err, sizeof(err)) == -1 && !img.rgba && err[0]);
    /* a cut-off JPEG: stb_image either refuses it or fills the missing rows; both are safe */
    int cut = akn_image_decode(JPEG_BASELINE, sizeof(JPEG_BASELINE) / 2, &img, err, sizeof(err));
    CHECK(cut == -1 ? !img.rgba : img.w == 16 && img.h == 8 && img.rgba != NULL);
    akn_image_free(&img);
    CHECK(akn_image_decode(JPEG_BASELINE, 3, &img, err, sizeof(err)) == -1);
    CHECK(akn_image_decode(NULL, 0, &img, err, sizeof(err)) == -1);
    static const uint8_t gif[] = "GIF89a\x01\x00\x01\x00\x80\x00\x00\xff\xff\xff\x00\x00\x00!\xf9\x04";
    CHECK(akn_image_decode(gif, sizeof(gif), &img, err, sizeof(err)) == -1);
    CHECK_CONTAINS(err, "JPEG or PNG");
    static const uint8_t webp[] = "RIFF\x24\x00\x00\x00WEBPVP8 ";
    CHECK(akn_image_decode(webp, sizeof(webp), &img, err, sizeof(err)) == -1);
    uint8_t garbage[64];
    memset(garbage, 0xAB, sizeof(garbage));
    garbage[0] = 0xFF, garbage[1] = 0xD8, garbage[2] = 0xFF;
    CHECK(akn_image_decode(garbage, sizeof(garbage), &img, err, sizeof(err)) == -1 && !img.rgba);
    /* a JPEG whose header claims 60000 x 60000: refused before any pixel memory is taken */
    uint8_t huge[sizeof(JPEG_BASELINE)];
    memcpy(huge, JPEG_BASELINE, sizeof(huge));
    for (size_t i = 0; i + 8 < sizeof(huge); i++)
        if (huge[i] == 0xFF && huge[i + 1] == 0xC0) {
            huge[i + 5] = 0xEA, huge[i + 6] = 0x60, huge[i + 7] = 0xEA, huge[i + 8] = 0x60;
            break;
        }
    CHECK(akn_image_decode(huge, sizeof(huge), &img, err, sizeof(err)) == -1 && !img.rgba);
    free(png);

    /* a PNG larger than the dimension limit, and one over the byte limit */
    uint8_t *wide = calloc(3000 * 4, 1);
    CHECK(wide && akn_png_encode(wide, 3000, 1, 3000 * 4, &png, &len) == 0);
    CHECK(akn_image_decode(png, len, &img, err, sizeof(err)) == -1 && !img.rgba);
    free(png);
    free(wide);
    uint8_t *big = calloc(AKN_IMAGE_MAX_BYTES + 1, 1);
    if (big) memcpy(big, JPEG_BASELINE, sizeof(JPEG_BASELINE));
    CHECK(big && akn_image_decode(big, AKN_IMAGE_MAX_BYTES + 1, &img, err, sizeof(err)) == -1);
    free(big);
}

static void test_fit(void) {
    /* 4x2: left half red, right half blue */
    uint8_t px[4 * 2 * 4];
    for (int i = 0; i < 8; i++) {
        int blue = i % 4 >= 2;
        px[i * 4 + 0] = blue ? 0 : 255, px[i * 4 + 1] = 0, px[i * 4 + 2] = blue ? 255 : 0, px[i * 4 + 3] = 255;
    }
    akn_image src = { 4, 2, px }, out;
    /* cover into 1x2: the centred window, half red half blue -> purple */
    CHECK(akn_image_fit(&src, 1, 2, AKN_FIT_COVER, &out) == 0 && out.w == 1 && out.h == 2);
    CHECK(near(out.rgba, 128, 0, 128, 3) && out.rgba[3] == 255);
    akn_image_free(&out);
    /* cover into 8x4 (upscale): left still red, right still blue */
    CHECK(akn_image_fit(&src, 8, 4, AKN_FIT_COVER, &out) == 0);
    CHECK(near(out.rgba, 255, 0, 0, 3) && near(out.rgba + 7 * 4, 0, 0, 255, 3));
    akn_image_free(&out);
    /* circle: transparent corners, opaque centre, nothing black bleeding at the edge */
    CHECK(akn_image_fit(&src, 32, 32, AKN_FIT_CIRCLE, &out) == 0);
    CHECK(out.rgba[3] == 0 && out.rgba[(31 * 32 + 31) * 4 + 3] == 0);
    CHECK(out.rgba[(16 * 32 + 16) * 4 + 3] == 255);
    const uint8_t *edge = out.rgba + (4 * 32 + 4) * 4; /* on the circle, partly covered: colour stays red */
    CHECK(edge[3] > 0 && edge[3] < 255 && near(edge, 255, 0, 0, 3));
    akn_image_free(&out);
    /* rounded: corner transparent, edge middle opaque */
    CHECK(akn_image_fit(&src, 100, 60, AKN_FIT_ROUNDED, &out) == 0);
    CHECK(out.rgba[3] == 0 && out.rgba[(50) * 4 + 3] == 255 && out.rgba[(30 * 100) * 4 + 3] == 255);
    CHECK(out.rgba[((59) * 100 + 99) * 4 + 3] == 0);
    akn_image_free(&out);
    CHECK(akn_image_fit(&src, 0, 4, AKN_FIT_COVER, &out) == -1 && !out.rgba);
    akn_image empty = { 0, 0, NULL };
    CHECK(akn_image_fit(&empty, 4, 4, AKN_FIT_COVER, &out) == -1);
}

static void test_vector(void) {
    akn_path *p = calloc(1, sizeof(*p));
    CHECK(akn_path_parse("M2 2 L14 2 L14 14 L2 14 Z", p) == 0 && p->nsub == 1 && p->closed[0]);
    uint8_t *px = calloc(16 * 16 * 4, 1);
    akn_raster_fill(px, 16, p, 1.0f, 0xFF102030u);
    CHECK(near(px + (8 * 16 + 8) * 4, 0x10, 0x20, 0x30, 0) && px[(8 * 16 + 8) * 4 + 3] == 255);
    CHECK(px[3] == 0 && px[(15 * 16 + 15) * 4 + 3] == 0);
    /* relative commands, H/V, curves and arcs with compact flags (as in Lucide's data) */
    memset(p, 0, sizeof(*p));
    CHECK(akn_path_parse("m1 1h4v4h-4z", p) == 0 && p->nsub == 1);
    CHECK(p->pts[1].x == 5 && p->pts[1].y == 1 && p->pts[2].x == 5 && p->pts[2].y == 5);
    memset(p, 0, sizeof(*p));
    CHECK(akn_path_parse("M11.562 3.266a.5.5 0 0 1 .876 0L15.39 8.87c1 1 2 2 3 3s1 1 2 2q1 1 2 2t1 1", p) == 0);
    CHECK(p->npts > 8);
    akn_pt last = p->pts[p->npts - 1];
    CHECK(last.x > 23.3f && last.x < 23.5f && last.y > 16.8f && last.y < 16.95f); /* (23.39, 16.87) */
    /* malformed data is refused */
    memset(p, 0, sizeof(*p));
    CHECK(akn_path_parse("M1 1 L x", p) == -1);
    memset(p, 0, sizeof(*p));
    CHECK(akn_path_parse("L1 1", p) == -1 || p->npts <= 2); /* no current point: tolerated or refused, never a crash */
    memset(p, 0, sizeof(*p));
    CHECK(akn_path_parse("M0 0 A1 1 0 2 1 2 2", p) == -1); /* flag must be 0 or 1 */
    /* stroke: a horizontal line, round caps */
    memset(p, 0, sizeof(*p));
    memset(px, 0, 16 * 16 * 4);
    CHECK(akn_path_parse("M4 8 L12 8", p) == 0);
    akn_raster_stroke(px, 16, p, 1.0f, 2.0f, 0xFFFFFFFFu);
    CHECK(px[(8 * 16 + 8) * 4 + 3] > 200 && px[(2 * 16 + 8) * 4 + 3] == 0);
    /* overflow is refused, not written past the arrays */
    memset(p, 0, sizeof(*p));
    char *longd = malloc(AKN_PATH_MAX_POINTS * 8 + 16);
    char *w = longd;
    w += sprintf(w, "M0 0");
    for (int i = 0; i < AKN_PATH_MAX_POINTS + 10; i++) w += sprintf(w, "l1 0");
    CHECK(akn_path_parse(longd, p) == -1 && p->npts <= AKN_PATH_MAX_POINTS);
    free(longd);
    free(px);
    free(p);
}

static void test_badges(void) {
    CHECK(akn_badge_count() == 8);
    const akn_badge_def *d = akn_badge_find("ps5_alpha_tester");
    CHECK(d && strcmp(d->name, "PS5 Alpha Tester") == 0 && d->color == 0x5B8CFF);
    CHECK(akn_badge_find("founder") && akn_badge_find("event_participant"));
    CHECK(!akn_badge_find("nope") && !akn_badge_find(NULL));
    uint8_t *px = malloc(96 * 96 * 4);
    for (int i = 0; i < akn_badge_count(); i++) {
        const akn_badge_def *b = akn_badge_at(i);
        CHECK(akn_badge_render(b->id, px, 64) == 0);
        /* inside the hexagon, off the icon and the highlight: the badge colour */
        const uint8_t *in = px + (40 * 64 + 12) * 4;
        CHECK(in[3] == 255 && near(in, (int)(b->color >> 16), (int)(b->color >> 8 & 255), (int)(b->color & 255), 8));
        CHECK(px[3] == 0 && px[(63 * 64 + 63) * 4 + 3] == 0); /* corners outside the hexagon */
        /* some icon pixels near the centre are the dark icon colour */
        int dark = 0;
        for (int y = 20; y < 44; y++)
            for (int x = 20; x < 44; x++)
                if (px[(y * 64 + x) * 4] < 40 && px[(y * 64 + x) * 4 + 3] == 255) dark++;
        CHECK(dark > 10);
    }
    CHECK(akn_badge_render("unknown", px, 40) == 0 && near(px + (26 * 40 + 8) * 4, 0x5B, 0x67, 0x85, 8));
    CHECK(akn_badge_render("founder", px, 4) == -1 && akn_badge_render("founder", NULL, 40) == -1);
    CHECK(akn_badge_at(-1) == NULL && akn_badge_at(8) == NULL);
    free(px);
}

/* A profile as the server sends it, with pictures, banner, theme, badges and artwork. */
static const char PROFILE_ART[] =
    "{\"ok\":true,\"status\":200,\"data\":{"
    "\"user\":{\"id\":\"u-bob\",\"onlineId\":\"bob\",\"displayName\":\"Bob\",\"avatar\":\"a2\","
    "\"avatarImage\":\"0123456789abcdef0123456789abcdef\",\"deleted\":false},"
    "\"presence\":{\"status\":\"online\",\"lastSeenAt\":null,\"activity\":null,\"game\":{\"titleId\":\"PPSA01234\",\"name\":\"Astro Bot\","
    "\"platform\":\"PS5\",\"startedAt\":null,\"art\":{\"gameId\":\"g1\",\"icon\":\"11111111111111111111111111111111\",\"cover\":null,"
    "\"banner\":\"NOT-A-MEDIA-ID\"}}},"
    "\"joinedAt\":\"2026-03-01T10:00:00Z\",\"restricted\":false,\"bio\":\"\",\"statusMessage\":null,"
    "\"favoriteGames\":[\"Astro Bot\",\"Tetris\"],"
    "\"favoriteGameDetails\":[{\"name\":\"Tetris\",\"art\":{\"gameId\":\"g2\",\"icon\":null,\"cover\":\"22222222222222222222222222222222\",\"banner\":null}}],"
    "\"relationship\":{\"state\":\"friend\"},\"mutualFriendCount\":0,"
    "\"appearance\":{\"theme\":\"custom\",\"accentColor\":\"#12ab3C\",\"bannerImage\":\"33333333333333333333333333333333\",\"bannerOverlay\":true},"
    "\"badges\":[{\"id\":\"founder\"},{\"id\":\"beta_tester\"},{\"id\":\"ps5_alpha_tester\"},{\"id\":\"x_y\"}],"
    "\"featuredBadges\":[\"ps5_alpha_tester\"],\"party\":null}}";

static void test_profile_pictures(void) {
    akn_proxy_result res;
    const akeno_json *data;
    akn_profile p;
    akeno_json_doc *doc = akn_proxy_parse(PROFILE_ART, strlen(PROFILE_ART), &res, &data);
    CHECK(akn_fill_profile(data, &p) == 0);
    akeno_json_free(doc);
    CHECK_STR(p.avatar_image, "0123456789abcdef0123456789abcdef");
    CHECK_STR(p.banner_image, "33333333333333333333333333333333");
    CHECK(p.banner_overlay && strcmp(p.theme, "custom") == 0 && p.accent == 0x12AB3C);
    CHECK(p.badge_count == 4 && p.featured_badges == 1 && p.badges == 4);
    CHECK_STR(p.badge_ids[0], "ps5_alpha_tester"); /* featured first, then the rest, no duplicate */
    CHECK_STR(p.badge_ids[1], "founder");
    CHECK_STR(p.badge_ids[2], "beta_tester");
    CHECK_STR(p.presence.game.art.icon, "11111111111111111111111111111111");
    CHECK(!p.presence.game.art.cover[0] && !p.presence.game.art.banner[0]); /* null and invalid IDs are dropped */
    CHECK(p.favorite_count == 2 && !p.favorite_art[0].cover[0]);
    CHECK_STR(p.favorite_art[1].cover, "22222222222222222222222222222222");

    /* restricted: the server sends no banner; theme falls back to the default */
    const char *restricted = "{\"ok\":true,\"status\":200,\"data\":{\"user\":{\"id\":\"u-x\",\"onlineId\":\"x\",\"displayName\":\"X\","
                             "\"avatarImage\":null},\"restricted\":true,\"appearance\":{\"theme\":\"emerald\",\"bannerImage\":null},"
                             "\"badges\":[],\"featuredBadges\":[]}}";
    doc = akn_proxy_parse(restricted, strlen(restricted), &res, &data);
    CHECK(akn_fill_profile(data, &p) == 0 && !p.avatar_image[0] && !p.banner_image[0] && p.badge_count == 0);
    CHECK(p.accent == 0x10B981);
    akeno_json_free(doc);

    uint32_t from, to;
    akn_theme_gradient("sakura", &from, &to);
    CHECK(from == 0x4A1838 && to == 0x1F0C1A);
    akn_theme_gradient("unknown", &from, &to);
    CHECK(from == 0x1B2A55 && to == 0x2A1F4D);
    CHECK(akn_theme_accent(NULL) == 0x4F7CFF && akn_theme_accent("midnight") == 0xC3CBE0);
}

void run_picture_tests(void);
void run_picture_tests(void) {
    test_base64_and_ids();
    test_decode();
    test_fit();
    test_vector();
    test_badges();
    test_profile_pictures();
}
