/* libFuzzer harness: any bytes through the picture decoder and the fit the app applies to them. */
#include <stddef.h>
#include <stdint.h>

#include "image.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    akn_image img, out;
    char err[96];
    if (akn_image_decode(data, size, &img, err, sizeof(err)) != 0) return 0;
    if (akn_image_fit(&img, 37, 23, (akn_fit)(size % 3), &out) == 0) akn_image_free(&out);
    akn_image_free(&img);
    return 0;
}
