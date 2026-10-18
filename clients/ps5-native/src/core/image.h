/*
 * Pictures for the native profiles: decoding of untrusted JPEG and PNG data (profile pictures,
 * banners, game artwork from the AKENO server through the daemon; game icons from this console),
 * and preparation for drawing (center-crop to the exact size it is shown at, circular mask for
 * avatars). The decoder is stb_image v2.29 limited to JPEG and PNG; everything is checked before
 * it decodes: the format by its magic bytes, the input size, and the dimensions from the header
 * (no decode starts for an image larger than AKN_IMAGE_MAX_DIM or AKN_IMAGE_MAX_PIXELS). Portable,
 * unit-tested and fuzzed (tests/fuzz).
 */
#ifndef AKN_IMAGE_H
#define AKN_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define AKN_IMAGE_MAX_BYTES (768 * 1024)     /* the daemon delivers at most 512 KiB */
#define AKN_IMAGE_MAX_DIM 2048               /* the server's largest fallback variant is 960 px */
#define AKN_IMAGE_MAX_PIXELS (2048 * 1024)

typedef struct {
    int w, h;
    uint8_t *rgba; /* w*h*4 bytes, R G B A in memory, straight alpha */
} akn_image;

typedef enum { AKN_FIT_COVER = 0, AKN_FIT_CIRCLE, AKN_FIT_ROUNDED } akn_fit;

/* Corner radius of AKN_FIT_ROUNDED (the app's card corners). */
#define AKN_FIT_RADIUS(w, h) (((w) < (h) ? (w) : (h)) < 80 ? 10 : 14)

/* Decodes a JPEG or PNG. 0 on success; -1 with a short reason in err (nothing allocated). */
int akn_image_decode(const uint8_t *data, size_t len, akn_image *out, char *err, size_t err_len);
void akn_image_free(akn_image *img);

/* A new w x h image filled from src: scaled to cover the whole rectangle, centred, the overflow cut
 * off (as CSS object-fit: cover); AKN_FIT_CIRCLE also masks it to an anti-aliased circle,
 * AKN_FIT_ROUNDED to a rectangle with rounded corners (AKN_FIT_RADIUS). */
int akn_image_fit(const akn_image *src, int w, int h, akn_fit fit, akn_image *out);

/* Standard base64 (with or without padding; no whitespace). Returns the decoded length or -1. */
long akn_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap);

/* 1 for a 32-digit lowercase hex media ID, 1 for a PlayStation title ID (ABCD12345). */
int akn_is_media_id(const char *s);
int akn_is_title_id(const char *s);

#endif
