/*
 * The only entry points of stb_image that AKENO Native uses (src/third/stb_image_impl.c, compiled
 * without AKENO's warning flags because it is third-party code). Configured for JPEG and PNG from
 * memory only: no files, no HDR/linear, no other formats, dimensions capped at 2048.
 */
#ifndef AKN_STB_IMAGE_AKN_H
#define AKN_STB_IMAGE_AKN_H

#include <stdint.h>

int akn_stbi_info(const uint8_t *data, int len, int *w, int *h, int *comp);
/* RGBA pixels (free with akn_stbi_free), or NULL. */
uint8_t *akn_stbi_load_rgba(const uint8_t *data, int len, int *w, int *h);
void akn_stbi_free(void *p);
const char *akn_stbi_failure(void);

#endif
