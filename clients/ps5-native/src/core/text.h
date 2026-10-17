/*
 * Text helpers independent of the font engine: UTF-8 decoding (invalid input becomes U+FFFD),
 * a shelf packer for glyph atlases, and fitting/wrapping with a caller-supplied measure function.
 */
#ifndef AKN_TEXT_H
#define AKN_TEXT_H

#include <stddef.h>
#include <stdint.h>

/* Decodes one code point; returns the bytes consumed (>= 1 while len > 0, 0 at the end). */
size_t akn_utf8_next(const char *s, size_t len, uint32_t *cp);

typedef struct {
    int width, height, padding;
    int shelf_y, shelf_h, cursor_x;
    int full;
} akn_packer;

void akn_packer_init(akn_packer *p, int width, int height, int padding);
/* Reserves a w x h slot; returns 0 and its position, or -1 when the atlas is full. */
int akn_packer_add(akn_packer *p, int w, int h, int *x, int *y);

/* Width in pixels of the first len bytes of s. */
typedef int (*akn_measure_fn)(void *user, const char *s, size_t len);

/*
 * Bytes of s that fit in max_width (never splitting a UTF-8 sequence). When the whole text does
 * not fit, the result leaves room for an ellipsis of width ellipsis_width and *truncated is 1.
 */
size_t akn_text_fit(const char *s, int max_width, int ellipsis_width, akn_measure_fn measure, void *user, int *truncated);

/*
 * Length of the next line of s for word wrapping at max_width: breaks after the last space that
 * fits (a word longer than the line is broken where it must be); a '\n' always ends the line.
 * *next receives the offset where the following line starts. Returns 0 only for empty input.
 */
size_t akn_text_wrap(const char *s, int max_width, akn_measure_fn measure, void *user, size_t *next);

#endif
