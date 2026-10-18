/*
 * libFuzzer harness: hostile input for everything the updater reads from the network before or
 * after the signature check. The first byte selects the reader:
 *   0: the update package (akn_zip_open, then every entry decompressed and CRC-checked)
 *   1: the GitHub releases list (akn_update_pick, both channels)
 *   2: the manifest (akn_update_manifest_parse against a fixed release, then the compatibility check)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "update.h"
#include "update_zip.h"

#define FUZZ_MAX_ENTRY (1u << 20)

static void fuzz_zip(const uint8_t *data, size_t size) {
    akn_zip zip;
    char why[160];
    if (akn_zip_open(&zip, data, size, why, sizeof(why)) != 0) return;
    for (int i = 0; i < zip.count; i++) {
        const akn_zip_entry *e = &zip.entries[i];
        if (e->is_dir || e->usize > FUZZ_MAX_ENTRY) continue;
        uint8_t *out = malloc(e->usize ? e->usize : 1);
        if (!out) return;
        (void)akn_zip_read(&zip, e, out, e->usize, why, sizeof(why));
        free(out);
    }
}

static void fuzz_releases(const char *json, size_t len) {
    static const akn_update_source src = { "https://api.github.com/repos/YTDxDAkeno/AKENO-NETWORK/releases",
                                           "https://github.com/YTDxDAkeno/AKENO-NETWORK/releases/download/" };
    akn_update_release rel;
    char why[200];
    int skipped = 0;
    (void)akn_update_pick(&src, json, len, "1.1.0", AKN_CHANNEL_STABLE, &rel, &skipped, why, sizeof(why));
    (void)akn_update_pick(&src, json, len, "1.2.0-beta.1", AKN_CHANNEL_BETA, &rel, &skipped, why, sizeof(why));
}

static void fuzz_manifest(const char *json, size_t len) {
    static const akn_update_release rel = { .tag = "native-1.2.0", .version = "1.2.0",
                                            .package_name = "akeno-native-1.2.0.zip" };
    akn_update_manifest m;
    char why[200];
    if (akn_update_manifest_parse(json, len, &rel, &m, why, sizeof(why)) != 0) return;
    akn_update_env env = { "1.1.0", 0x12200000u, 1 };
    (void)akn_update_compatible(&m, &env, why, sizeof(why));
    env.firmware_raw = 0;
    env.daemon_protocol = -1;
    (void)akn_update_compatible(&m, &env, why, sizeof(why));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;
    switch (data[0] % 3) {
    case 0: fuzz_zip(data + 1, size - 1); break;
    case 1: fuzz_releases((const char *)data + 1, size - 1); break;
    default: fuzz_manifest((const char *)data + 1, size - 1); break;
    }
    return 0;
}
