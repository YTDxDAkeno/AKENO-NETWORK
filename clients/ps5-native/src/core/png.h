/*
 * Minimal PNG writer: 8-bit RGBA, no compression (stored deflate blocks), standard CRC-32 and
 * Adler-32. Used for the launcher icon (built on the PC) and test screenshots. No dependencies.
 */
#ifndef AKN_PNG_H
#define AKN_PNG_H

#include <stddef.h>
#include <stdint.h>

uint32_t akn_crc32(uint32_t crc, const uint8_t *data, size_t len);
uint32_t akn_adler32(uint32_t adler, const uint8_t *data, size_t len);

/*
 * Encodes width*height RGBA pixels (row pitch `pitch` bytes) into a new buffer (*out, release with
 * free()). Returns 0 on success, -1 on invalid arguments or allocation failure.
 */
int akn_png_encode(const uint8_t *rgba, int width, int height, int pitch, uint8_t **out, size_t *out_len);

/* Encodes and writes to path (written to path.tmp, then renamed). Returns 0 on success. */
int akn_png_write(const char *path, const uint8_t *rgba, int width, int height, int pitch);

#endif
