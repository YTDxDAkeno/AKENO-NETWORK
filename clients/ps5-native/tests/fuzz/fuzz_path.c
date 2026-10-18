/* libFuzzer harness: any text as SVG path data, flattened and rasterised as the badges are. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"
#include "vector.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static akn_path p;
    static uint8_t px[24 * 24 * 4];
    if (size > 4096) return 0;
    char *d = malloc(size + 1);
    if (!d) return 0;
    memcpy(d, data, size);
    d[size] = '\0';
    memset(&p, 0, sizeof(p));
    akn_path_parse(d, &p);
    akn_path_transform(&p, 0.6f, 8.8f, 8.8f);
    akn_raster_fill(px, 24, &p, 0.75f, 0xFF5B8CFFu);
    akn_raster_stroke(px, 24, &p, 0.75f, 1.5f, 0xFF0B0F1Au);
    /* the base64 decoder sees the same bytes */
    uint8_t out[3072];
    akn_base64_decode(d, size, out, sizeof(out));
    free(d);
    return 0;
}
