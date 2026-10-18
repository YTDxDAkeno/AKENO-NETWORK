/* stb_image v2.29 (nothings/stb 6e9f34d, public domain / MIT), restricted to what AKENO Native needs. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 2048
#define STBI_NO_THREAD_LOCALS /* one decoding thread; no TLS in the payload ELF */
#include "stb_image.h"

#include "stb_image_akn.h"

int akn_stbi_info(const uint8_t *data, int len, int *w, int *h, int *comp) { return stbi_info_from_memory(data, len, w, h, comp); }

uint8_t *akn_stbi_load_rgba(const uint8_t *data, int len, int *w, int *h) {
    int comp = 0;
    return stbi_load_from_memory(data, len, w, h, &comp, 4);
}

void akn_stbi_free(void *p) { stbi_image_free(p); }

const char *akn_stbi_failure(void) {
    const char *r = stbi_failure_reason();
    return r ? r : "decode failed";
}

int akn_stbi_inflate_raw(uint8_t *out, int out_len, const uint8_t *in, int in_len) {
    return stbi_zlib_decode_noheader_buffer((char *)out, out_len, (const char *)in, in_len);
}
