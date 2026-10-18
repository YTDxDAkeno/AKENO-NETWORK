/* The updater: versions, release choice, manifest, signature, ZIP safety, install and rollback. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "semver.h"
#include "test.h"
#include "update.h"
#include "update_fs.h"
#include "update_install.h"
#include "update_net.h"
#include "update_verify.h"
#include "update_zip.h"
#include "png.h"

/* ---- semver ------------------------------------------------------------------------------------ */

static void test_semver(void) {
    akn_semver v;
    CHECK(akn_semver_parse("1.2.0-beta.1+ci.7", &v) == 0);
    CHECK(v.major == 1 && v.minor == 2 && v.patch == 0);
    CHECK_STR(v.pre, "beta.1");
    CHECK_STR(v.build, "ci.7");
    static const char *const bad[] = { "", "1", "1.2", "v1.2.3", "01.2.3", "1.02.3", "1.2.3-", "1.2.3-beta..1",
                                       "1.2.3-01", "1.2.3+", "1.2.3 ", "1.2.3-be ta", "99999999999999999999.0.0",
                                       "1.2.3-a_b", "-1.2.3" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        if (akn_semver_parse(bad[i], &v) == 0) fprintf(stderr, "accepted bad version \"%s\"\n", bad[i]);
        CHECK(akn_semver_parse(bad[i], &v) != 0);
    }
    /* semver.org 11.4, in ascending order */
    static const char *const order[] = { "1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                                         "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1",
                                         "1.1.0-beta.1", "1.1.0", "1.2.0-beta.1", "1.2.0-beta.2", "1.2.0-beta.10",
                                         "1.2.0-rc.1", "1.2.0", "2.0.0" };
    size_t n = sizeof(order) / sizeof(order[0]);
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) {
            int c = akn_semver_compare_text(order[i], order[j]);
            int want = i < j ? -1 : i > j ? 1 : 0;
            if (c != want) fprintf(stderr, "compare %s %s = %d\n", order[i], order[j], c);
            CHECK(c == want);
        }
    CHECK(akn_semver_compare_text("1.2.0+a", "1.2.0+b") == 0);
    CHECK(akn_semver_compare_text("x", "1.0.0") == -2);
    char out[64];
    akn_semver_parse("1.2.0-rc.1+abc", &v);
    akn_semver_format(&v, out, sizeof(out));
    CHECK_STR(out, "1.2.0-rc.1");
}

/* ---- release choice ---------------------------------------------------------------------------- */

static const akn_update_source k_src = { "https://api.github.com/repos/YTDxDAkeno/AKENO-NETWORK/releases",
                                         "https://github.com/YTDxDAkeno/AKENO-NETWORK/releases/download/" };

/* A GitHub releases list: per release the manifest, (optionally) its signature and the package. */
static void release_json(char *json, size_t cap, const char *const *tags, const int *draft, const int *pre,
                         const char *const *repos, const int *sig, int n) {
    snprintf(json, cap, "[");
    for (int i = 0; i < n; i++) {
        char one[4096] = "";
        const char *repo = repos ? repos[i] : "YTDxDAkeno/AKENO-NETWORK";
        char version[64];
        snprintf(version, sizeof(version), "%s", strncmp(tags[i], "native-", 7) == 0 ? tags[i] + 7 : tags[i]);
        char sigasset[512] = "";
        if (!sig || sig[i])
            snprintf(sigasset, sizeof(sigasset),
                     "{\"name\":\"akeno-native-update.json.sig\",\"size\":72,\"state\":\"uploaded\","
                     "\"browser_download_url\":\"https://github.com/%s/releases/download/%s/akeno-native-update.json.sig\"},",
                     repo, tags[i]);
        snprintf(one, sizeof(one),
                 "%s{\"tag_name\":\"%s\",\"name\":\"AKENO %s\",\"draft\":%s,\"prerelease\":%s,\"body\":\"Notes for %s\","
                 "\"published_at\":\"2026-10-18T10:00:00Z\",\"html_url\":\"https://github.com/%s/releases/tag/%s\",\"assets\":["
                 "{\"name\":\"akeno-native-update.json\",\"size\":900,\"state\":\"uploaded\",\"browser_download_url\":"
                 "\"https://github.com/%s/releases/download/%s/akeno-native-update.json\"},%s"
                 "{\"name\":\"akeno-native-%s.zip\",\"size\":5000000,\"state\":\"uploaded\",\"browser_download_url\":"
                 "\"https://github.com/%s/releases/download/%s/akeno-native-%s.zip\"}]}",
                 i ? "," : "", tags[i], version, draft && draft[i] ? "true" : "false", pre && pre[i] ? "true" : "false",
                 version, repo, tags[i], repo, tags[i], sigasset, version, repo, tags[i], version);
        strncat(json, one, cap - strlen(json) - 1);
    }
    strncat(json, "]", cap - strlen(json) - 1);
}

static void test_pick(void) {
    static char json[65536];
    akn_update_release rel;
    char why[256];
    int skipped = 0;

    const char *tags[] = { "native-1.1.0-beta.1", "native-1.2.0-beta.1", "native-1.2.0", "alpha-0.5.0",
                           "native-1.3.0-beta.1", "native-9.0.0" };
    int draft[] = { 0, 0, 0, 0, 0, 1 };
    int pre[] = { 1, 1, 0, 0, 1, 0 };
    release_json(json, sizeof(json), tags, draft, pre, NULL, NULL, 6);
    CHECK(akn_update_pick(&k_src, json, strlen(json), "1.1.0-beta.1", AKN_CHANNEL_STABLE, &rel, &skipped, why, sizeof(why)) ==
          AKN_PICK_FOUND);
    CHECK_STR(rel.version, "1.2.0"); /* stable: no pre-releases, the draft 9.0.0 ignored */
    CHECK_STR(rel.package_name, "akeno-native-1.2.0.zip");
    CHECK_STR(rel.package.url, "https://github.com/YTDxDAkeno/AKENO-NETWORK/releases/download/native-1.2.0/akeno-native-1.2.0.zip");
    CHECK(rel.package.size == 5000000);
    CHECK_STR(rel.notes, "Notes for 1.2.0");
    CHECK(akn_update_pick(&k_src, json, strlen(json), "1.1.0-beta.1", AKN_CHANNEL_BETA, &rel, &skipped, why, sizeof(why)) ==
          AKN_PICK_FOUND);
    CHECK_STR(rel.version, "1.3.0-beta.1");
    CHECK(rel.prerelease == 1);
    CHECK(akn_update_pick(&k_src, json, strlen(json), "1.3.0-beta.1", AKN_CHANNEL_BETA, &rel, &skipped, why, sizeof(why)) ==
          AKN_PICK_UP_TO_DATE);
    CHECK(akn_update_pick(&k_src, json, strlen(json), "1.2.0", AKN_CHANNEL_STABLE, &rel, &skipped, why, sizeof(why)) ==
          AKN_PICK_UP_TO_DATE);

    /* assets from another repository, or without a signature, are refused */
    const char *tags2[] = { "native-1.4.0", "native-1.3.0" };
    const char *repos[] = { "evil/AKENO-NETWORK", "YTDxDAkeno/AKENO-NETWORK" };
    release_json(json, sizeof(json), tags2, NULL, NULL, repos, NULL, 2);
    CHECK(akn_update_pick(&k_src, json, strlen(json), "1.2.0", AKN_CHANNEL_STABLE, &rel, &skipped, why, sizeof(why)) ==
          AKN_PICK_FOUND);
    CHECK_STR(rel.version, "1.3.0");
    CHECK(skipped == 1);
    int sig[] = { 0, 1 };
    release_json(json, sizeof(json), tags2, NULL, NULL, NULL, sig, 2);
    CHECK(akn_update_pick(&k_src, json, strlen(json), "1.2.0", AKN_CHANNEL_STABLE, &rel, &skipped, why, sizeof(why)) ==
          AKN_PICK_FOUND);
    CHECK_STR(rel.version, "1.3.0");
    /* a pre-release flag on GitHub keeps a release off the stable channel even with a plain version */
    const char *tags3[] = { "native-1.5.0" };
    int pre3[] = { 1 };
    release_json(json, sizeof(json), tags3, NULL, pre3, NULL, NULL, 1);
    CHECK(akn_update_pick(&k_src, json, strlen(json), "1.2.0", AKN_CHANNEL_STABLE, &rel, &skipped, why, sizeof(why)) ==
          AKN_PICK_UP_TO_DATE);
    /* build metadata and malformed tags are not releases of AKENO Native */
    const char *tags4[] = { "native-1.6.0+x", "native-v1.7.0", "native-1.8" };
    release_json(json, sizeof(json), tags4, NULL, NULL, NULL, NULL, 3);
    CHECK(akn_update_pick(&k_src, json, strlen(json), "1.2.0", AKN_CHANNEL_BETA, &rel, &skipped, why, sizeof(why)) ==
          AKN_PICK_UP_TO_DATE);
    /* garbage */
    CHECK(akn_update_pick(&k_src, "{\"message\":\"x\"}", 15, "1.2.0", AKN_CHANNEL_BETA, &rel, &skipped, why, sizeof(why)) ==
          AKN_PICK_ERROR);
    CHECK(akn_update_pick(&k_src, "[]", 2, "not-a-version", AKN_CHANNEL_BETA, &rel, &skipped, why, sizeof(why)) ==
          AKN_PICK_ERROR);
}

/* ---- manifest and compatibility --------------------------------------------------------------- */

static const char k_sha_a[] = "1111111111111111111111111111111111111111111111111111111111111111";

static int manifest_text(char *out, size_t cap, const char *version, const char *tag, const char *channel,
                         const char *files, const char *requires) {
    return snprintf(out, cap,
                    "{\"format\":1,\"product\":\"akeno-network-native\",\"version\":\"%s\",\"tag\":\"%s\",\"channel\":\"%s\","
                    "\"package\":{\"name\":\"akeno-native-%s.zip\",\"size\":5000000,\"sha256\":\"%s\"},"
                    "\"files\":[%s],\"requires\":{%s},\"notes\":\"Signed notes\"}",
                    version, tag, channel, version, k_sha_a, files, requires);
}

static void release_for(akn_update_release *rel, const char *version) {
    memset(rel, 0, sizeof(*rel));
    snprintf(rel->version, sizeof(rel->version), "%s", version);
    snprintf(rel->tag, sizeof(rel->tag), "native-%s", version);
    snprintf(rel->package_name, sizeof(rel->package_name), "akeno-native-%s.zip", version);
    rel->package.size = 5000000;
}

static void test_manifest(void) {
    char text[4096], why[256];
    char files[512];
    snprintf(files, sizeof(files),
             "{\"path\":\"eboot.elf\",\"size\":100,\"sha256\":\"%s\"},{\"path\":\"sce_sys/icon0.png\",\"size\":10,\"sha256\":\"%s\"}",
             k_sha_a, k_sha_a);
    akn_update_release rel;
    release_for(&rel, "1.2.0-beta.2");
    akn_update_manifest m;
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.2", "beta", files,
                  "\"firmware_min\":\"12.00\",\"firmware_max\":\"12.70\",\"daemon_protocol_min\":3,\"min_installed_version\":\"1.1.0-beta.1\"");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) == 0);
    CHECK(m.file_count == 2);
    CHECK(m.daemon_protocol_min == 3 && m.daemon_protocol_max == 0);
    CHECK_STR(m.notes, "Signed notes");

    akn_update_env env = { "1.1.0-beta.1", 0x12200000u, 3 };
    CHECK(akn_update_compatible(&m, &env, why, sizeof(why)) == AKN_COMPAT_OK);
    env.daemon_protocol = 2;
    CHECK(akn_update_compatible(&m, &env, why, sizeof(why)) == AKN_COMPAT_DAEMON);
    CHECK_CONTAINS(why, "daemon protocol 3 or newer");
    env.daemon_protocol = -1;
    CHECK(akn_update_compatible(&m, &env, why, sizeof(why)) == AKN_COMPAT_DAEMON);
    env.daemon_protocol = 3;
    env.firmware_raw = 0x13000000u;
    CHECK(akn_update_compatible(&m, &env, why, sizeof(why)) == AKN_COMPAT_FIRMWARE);
    env.firmware_raw = 0x11600000u;
    CHECK(akn_update_compatible(&m, &env, why, sizeof(why)) == AKN_COMPAT_FIRMWARE);
    env.firmware_raw = 0x12700000u;
    CHECK(akn_update_compatible(&m, &env, why, sizeof(why)) == AKN_COMPAT_OK);
    env.current_version = "1.0.0";
    CHECK(akn_update_compatible(&m, &env, why, sizeof(why)) == AKN_COMPAT_FROM_VERSION);
    env.current_version = "1.2.0-beta.2";
    CHECK(akn_update_compatible(&m, &env, why, sizeof(why)) == AKN_COMPAT_NOT_NEWER);
    CHECK(akn_update_firmware_code("12.20") == 0x1220);
    CHECK(akn_update_firmware_code("12.2") == -1);
    CHECK(akn_update_firmware_code("x") == -1);

    /* refusals */
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.1", "beta", files, "");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) != 0); /* replayed under another tag */
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.2", "stable", files, "");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) != 0); /* pre-release on stable */
    char bad[512];
    snprintf(bad, sizeof(bad), "{\"path\":\"eboot.elf\",\"size\":1,\"sha256\":\"%s\"},{\"path\":\"run.sh\",\"size\":1,\"sha256\":\"%s\"}",
             k_sha_a, k_sha_a);
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.2", "beta", bad, "");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) != 0);
    CHECK_CONTAINS(why, "run.sh");
    snprintf(bad, sizeof(bad), "{\"path\":\"../eboot.elf\",\"size\":1,\"sha256\":\"%s\"}", k_sha_a);
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.2", "beta", bad, "");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) != 0);
    snprintf(bad, sizeof(bad), "{\"path\":\"homebrew.js\",\"size\":1,\"sha256\":\"%s\"}", k_sha_a);
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.2", "beta", bad, "");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) != 0); /* no eboot.elf */
    snprintf(bad, sizeof(bad), "{\"path\":\"eboot.elf\",\"size\":1,\"sha256\":\"%s\"},{\"path\":\"eboot.elf\",\"size\":1,\"sha256\":\"%s\"}",
             k_sha_a, k_sha_a);
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.2", "beta", bad, "");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) != 0); /* duplicate */
    snprintf(bad, sizeof(bad), "{\"path\":\"eboot.elf\",\"size\":1,\"sha256\":\"%s\"}",
             "ABCDEF1111111111111111111111111111111111111111111111111111111111");
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.2", "beta", bad, "");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) != 0); /* upper-case hex */
    snprintf(bad, sizeof(bad), "{\"path\":\"eboot.elf\",\"size\":999999999999,\"sha256\":\"%s\"}", k_sha_a);
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.2", "beta", bad, "");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) != 0); /* too large */
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.2", "beta", files, "\"firmware_min\":\"12\"");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) != 0);
    rel.package.size = 4999999;
    manifest_text(text, sizeof(text), "1.2.0-beta.2", "native-1.2.0-beta.2", "beta", files, "");
    CHECK(akn_update_manifest_parse(text, strlen(text), &rel, &m, why, sizeof(why)) != 0); /* GitHub size differs */
    CHECK(akn_update_manifest_parse("[]", 2, &rel, &m, why, sizeof(why)) != 0);
    CHECK(akn_update_path_allowed("eboot.elf") && akn_update_path_allowed("sce_sys/icon0.png"));
    CHECK(!akn_update_path_allowed("sce_sys/param.json") && !akn_update_path_allowed("/eboot.elf"));
}

/* ---- signature --------------------------------------------------------------------------------- */

typedef struct {
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    char pem[512];
} test_key;

static int key_make(test_key *k) {
    mbedtls_pk_init(&k->pk);
    mbedtls_entropy_init(&k->entropy);
    mbedtls_ctr_drbg_init(&k->drbg);
    if (mbedtls_ctr_drbg_seed(&k->drbg, mbedtls_entropy_func, &k->entropy, (const unsigned char *)"akn-test", 8)) return -1;
    if (mbedtls_pk_setup(&k->pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) return -1;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(k->pk), mbedtls_ctr_drbg_random, &k->drbg)) return -1;
    return mbedtls_pk_write_pubkey_pem(&k->pk, (unsigned char *)k->pem, sizeof(k->pem));
}

static size_t key_sign(test_key *k, const void *data, size_t len, uint8_t *sig, size_t cap) {
    uint8_t digest[32];
    akn_sha256(data, len, digest);
    size_t n = 0;
    if (mbedtls_pk_sign(&k->pk, MBEDTLS_MD_SHA256, digest, 32, sig, cap, &n, mbedtls_ctr_drbg_random, &k->drbg)) return 0;
    return n;
}

static void key_free(test_key *k) {
    mbedtls_pk_free(&k->pk);
    mbedtls_ctr_drbg_free(&k->drbg);
    mbedtls_entropy_free(&k->entropy);
}

static void test_signature(void) {
    test_key a, b;
    CHECK(key_make(&a) == 0);
    CHECK(key_make(&b) == 0);
    const char msg[] = "{\"format\":1}";
    uint8_t sig[MBEDTLS_PK_SIGNATURE_MAX_SIZE];
    size_t n = key_sign(&a, msg, strlen(msg), sig, sizeof(sig));
    CHECK(n > 0);
    char why[160];
    CHECK(akn_update_verify_signature(a.pem, msg, strlen(msg), sig, n, why, sizeof(why)) == 0);
    CHECK(akn_update_verify_signature(b.pem, msg, strlen(msg), sig, n, why, sizeof(why)) != 0); /* other key */
    char tampered[] = "{\"format\":2}";
    CHECK(akn_update_verify_signature(a.pem, tampered, strlen(tampered), sig, n, why, sizeof(why)) != 0);
    sig[n / 2] ^= 1;
    CHECK(akn_update_verify_signature(a.pem, msg, strlen(msg), sig, n, why, sizeof(why)) != 0);
    CHECK(akn_update_verify_signature(a.pem, msg, strlen(msg), sig, 0, why, sizeof(why)) != 0);
    CHECK(akn_update_verify_signature(NULL, msg, strlen(msg), sig, n, why, sizeof(why)) != 0);
    CHECK(akn_update_verify_signature("-----BEGIN PUBLIC KEY-----\nAAAA\n-----END PUBLIC KEY-----\n", msg, strlen(msg), sig,
                                      n, why, sizeof(why)) != 0);
    uint8_t d[32];
    char hex[65];
    akn_sha256("abc", 3, d);
    akn_update_hex(d, hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    key_free(&a);
    key_free(&b);
}

/* ---- ZIP --------------------------------------------------------------------------------------- */

typedef struct {
    uint8_t data[65536];
    size_t len;
    uint8_t cdir[8192];
    size_t clen;
    int count;
} zipb;

static void w16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t *p, uint32_t v) {
    w16(p, v & 0xFFFF);
    w16(p + 2, v >> 16);
}

/* Adds an entry; payload is stored as given (method 8: already DEFLATE-compressed). */
static void zip_add(zipb *z, const char *name, const void *payload, size_t plen, uint32_t usize, uint16_t method,
                    uint32_t crc, unsigned unix_mode) {
    size_t nl = strlen(name);
    uint8_t *l = z->data + z->len;
    w32(l, 0x04034b50u);
    w16(l + 4, 20);
    w16(l + 6, 0);
    w16(l + 8, method);
    w32(l + 10, 0);
    w32(l + 14, crc);
    w32(l + 18, (uint32_t)plen);
    w32(l + 22, usize);
    w16(l + 26, (unsigned)nl);
    w16(l + 28, 0);
    memcpy(l + 30, name, nl);
    memcpy(l + 30 + nl, payload, plen);
    uint32_t off = (uint32_t)z->len;
    z->len += 30 + nl + plen;
    uint8_t *c = z->cdir + z->clen;
    memset(c, 0, 46);
    w32(c, 0x02014b50u);
    w16(c + 4, (3u << 8) | 20u);
    w16(c + 6, 20);
    w16(c + 10, method);
    w32(c + 16, crc);
    w32(c + 20, (uint32_t)plen);
    w32(c + 24, usize);
    w16(c + 28, (unsigned)nl);
    w32(c + 38, (uint32_t)unix_mode << 16);
    w32(c + 42, off);
    memcpy(c + 46, name, nl);
    z->clen += 46 + nl;
    z->count++;
}

static void zip_stored(zipb *z, const char *name, const char *text) {
    zip_add(z, name, text, strlen(text), (uint32_t)strlen(text), 0, akn_crc32(0, (const uint8_t *)text, strlen(text)), 0100644);
}

static void zip_finish(zipb *z) {
    uint32_t cd_off = (uint32_t)z->len;
    memcpy(z->data + z->len, z->cdir, z->clen);
    z->len += z->clen;
    uint8_t *e = z->data + z->len;
    memset(e, 0, 22);
    w32(e, 0x06054b50u);
    w16(e + 8, (unsigned)z->count);
    w16(e + 10, (unsigned)z->count);
    w32(e + 12, (uint32_t)z->clen);
    w32(e + 16, cd_off);
    z->len += 22;
}

static const uint8_t k_deflated[] = {
    0x73, 0xf4, 0x76, 0xf5, 0xf3, 0x57, 0x28, 0x2d, 0x48, 0x49, 0x2c, 0x49, 0x55, 0x28, 0x49, 0x2d, 0x2e, 0x51, 0x28,
    0x48, 0xac, 0xcc, 0xc9, 0x4f, 0x4c, 0xb1, 0x52, 0x60, 0x60, 0x64, 0x62, 0x66, 0x61, 0x65, 0x63, 0xe7, 0xe0, 0xe4,
    0xe2, 0xe6, 0xe1, 0xe5, 0xe3, 0x17, 0x10, 0x14, 0x12, 0x16, 0x11, 0x15, 0x13, 0x97, 0x90, 0x94, 0x92, 0x96, 0x91,
    0x95, 0x93, 0x57, 0x50, 0x54, 0x52, 0x56, 0x51, 0x55, 0x53, 0xd7, 0xd0, 0xd4, 0xd2, 0xd6, 0xd1, 0xd5, 0xd3, 0x37,
    0x30, 0x34, 0x32, 0x36, 0x31, 0x35, 0x33, 0xb7, 0xb0, 0xb4, 0xb2, 0xb6, 0xb1, 0xb5, 0xb3, 0x77, 0x1c, 0x35, 0x7a,
    0xd4, 0xe8, 0x51, 0xa3, 0x47, 0x8d, 0x1e, 0x35, 0x7a, 0xd4, 0xe8, 0xa1, 0x68, 0x34, 0x00
};

static void deflated_payload(uint8_t *out, size_t *len) {
    static const char prefix[] = "AKENO update test payload: ";
    size_t n = 0;
    for (int r = 0; r < 20; r++) {
        memcpy(out + n, prefix, sizeof(prefix) - 1);
        n += sizeof(prefix) - 1;
        for (int i = 0; i < 64; i++) out[n++] = (uint8_t)i;
    }
    *len = n;
}

static int zip_open_text(zipb *z, char *why, size_t why_len) {
    akn_zip zip;
    return akn_zip_open(&zip, z->data, z->len, why, why_len);
}

static void file_entry(akn_update_manifest *m, const char *path, const void *data, size_t len) {
    akn_update_file *f = &m->files[m->file_count++];
    snprintf(f->path, sizeof(f->path), "%s", path);
    f->size = (long long)len;
    uint8_t d[32];
    akn_sha256(data, len, d);
    akn_update_hex(d, f->sha256);
}

static char g_tmp[256];

static void make_tmp(void) {
    snprintf(g_tmp, sizeof(g_tmp), "/tmp/akn-update-test-XXXXXX");
    CHECK(mkdtemp(g_tmp) != NULL);
}

static void test_zip(void) {
    char why[256];
    static zipb z;
    /* names */
    CHECK(akn_zip_name_safe("eboot.elf", 9));
    CHECK(akn_zip_name_safe("sce_sys/icon0.png", 17));
    CHECK(akn_zip_name_safe("sce_sys/", 8));
    static const char *const unsafe[] = { "../eboot.elf", "/etc/x", "a/../b", "a//b", "a\\b", "C:x", ".", "..", "a/./b",
                                          "a b", "", "/", "\x01" };
    for (size_t i = 0; i < sizeof(unsafe) / sizeof(unsafe[0]); i++) CHECK(!akn_zip_name_safe(unsafe[i], strlen(unsafe[i])));

    /* a valid package: one stored and one deflated file, and a directory entry */
    uint8_t payload[2048];
    size_t plen = 0;
    deflated_payload(payload, &plen);
    CHECK(plen == 1820);
    CHECK(akn_crc32(0, (const uint8_t *)payload, plen) == 0xab5b7145u);
    memset(&z, 0, sizeof(z));
    zip_stored(&z, "homebrew.js", "async function main() {}");
    zip_add(&z, "sce_sys/", "", 0, 0, 0, 0, 040755);
    zip_stored(&z, "sce_sys/icon0.png", "png");
    zip_add(&z, "eboot.elf", k_deflated, sizeof(k_deflated), (uint32_t)plen, 8, 0xab5b7145u, 0100755);
    zip_finish(&z);
    akn_zip zip;
    CHECK(akn_zip_open(&zip, z.data, z.len, why, sizeof(why)) == 0);
    CHECK(zip.count == 4);
    uint8_t out[2048];
    CHECK(akn_zip_read(&zip, &zip.entries[3], out, plen, why, sizeof(why)) == 0);
    CHECK(memcmp(out, payload, plen) == 0);

    akn_update_manifest m;
    memset(&m, 0, sizeof(m));
    file_entry(&m, "eboot.elf", payload, plen);
    file_entry(&m, "homebrew.js", "async function main() {}", 24);
    file_entry(&m, "sce_sys/icon0.png", "png", 3);
    make_tmp();
    char dest[300];
    snprintf(dest, sizeof(dest), "%s/files", g_tmp);
    CHECK(akn_fs_mkdirs(dest) == 0);
    CHECK(akn_zip_extract_package(z.data, z.len, &m, dest, NULL, why, sizeof(why)) == 0);
    char path[400];
    snprintf(path, sizeof(path), "%s/eboot.elf", dest);
    size_t got = 0;
    char *back = akn_fs_read_file(path, 4096, &got);
    CHECK(back && got == plen && memcmp(back, payload, plen) == 0);
    free(back);

    /* the manifest decides: a missing, an extra or a changed file is refused */
    akn_update_manifest m2 = m;
    file_entry(&m2, "daemon/akeno-network-ps5-native-ipc.elf", "elf", 3);
    CHECK(akn_zip_extract_package(z.data, z.len, &m2, dest, NULL, why, sizeof(why)) != 0);
    CHECK_CONTAINS(why, "lacks daemon/akeno-network-ps5-native-ipc.elf");
    m2 = m;
    m2.file_count = 1;
    CHECK(akn_zip_extract_package(z.data, z.len, &m2, dest, NULL, why, sizeof(why)) != 0);
    CHECK_CONTAINS(why, "does not list: homebrew.js");
    m2 = m;
    m2.files[1].sha256[0] = m2.files[1].sha256[0] == '0' ? '1' : '0';
    CHECK(akn_zip_extract_package(z.data, z.len, &m2, dest, NULL, why, sizeof(why)) != 0);
    CHECK_CONTAINS(why, "signed checksum");
    m2 = m;
    m2.files[0].size = 1;
    CHECK(akn_zip_extract_package(z.data, z.len, &m2, dest, NULL, why, sizeof(why)) != 0);
    volatile int cancel = 1;
    CHECK(akn_zip_extract_package(z.data, z.len, &m, dest, &cancel, why, sizeof(why)) != 0);

    /* hostile archives */
    memset(&z, 0, sizeof(z));
    zip_stored(&z, "../eboot.elf", "x");
    zip_finish(&z);
    CHECK(zip_open_text(&z, why, sizeof(why)) != 0);
    memset(&z, 0, sizeof(z));
    zip_add(&z, "eboot.elf", "/etc", 4, 4, 0, akn_crc32(0, (const uint8_t *)"/etc", 4), 0120777); /* symbolic link */
    zip_finish(&z);
    CHECK(zip_open_text(&z, why, sizeof(why)) != 0);
    CHECK_CONTAINS(why, "link");
    memset(&z, 0, sizeof(z));
    zip_stored(&z, "eboot.elf", "a");
    zip_stored(&z, "eboot.elf", "b");
    zip_finish(&z);
    CHECK(zip_open_text(&z, why, sizeof(why)) != 0);
    memset(&z, 0, sizeof(z));
    zip_add(&z, "eboot.elf", "abc", 3, 3, 0, 0x12345678u, 0100644); /* wrong CRC */
    zip_finish(&z);
    CHECK(akn_zip_open(&zip, z.data, z.len, why, sizeof(why)) == 0);
    CHECK(akn_zip_read(&zip, &zip.entries[0], out, 3, why, sizeof(why)) != 0);
    memset(&z, 0, sizeof(z));
    zip_add(&z, "eboot.elf", "abc", 3, 3, 12, akn_crc32(0, (const uint8_t *)"abc", 3), 0100644); /* bzip2 */
    zip_finish(&z);
    CHECK(zip_open_text(&z, why, sizeof(why)) != 0);
    memset(&z, 0, sizeof(z));
    zip_add(&z, "eboot.elf", "a", 1, 100000, 8, 0, 0100644); /* implausible ratio */
    zip_finish(&z);
    CHECK(zip_open_text(&z, why, sizeof(why)) != 0);
    memset(&z, 0, sizeof(z));
    zip_stored(&z, "eboot.elf", "hello");
    zip_finish(&z);
    w32(z.data + z.len - 22 + 16, 0xFFFFFFFFu); /* ZIP64 marker */
    CHECK(zip_open_text(&z, why, sizeof(why)) != 0);
    memset(&z, 0, sizeof(z));
    zip_stored(&z, "eboot.elf", "hello");
    zip_finish(&z);
    z.data[6] = 1; /* encrypted flag in the local header */
    CHECK(zip_open_text(&z, why, sizeof(why)) != 0);
    memset(&z, 0, sizeof(z));
    zip_stored(&z, "eboot.elf", "hello");
    zip_stored(&z, "homebrew.js", "world");
    /* point the second entry at the first one's header: overlapping data */
    {
        size_t second = 46 + strlen("eboot.elf");
        w32(z.cdir + second + 42, 0);
    }
    zip_finish(&z);
    CHECK(zip_open_text(&z, why, sizeof(why)) != 0);
    CHECK(zip_open_text(&(zipb){ .len = 10 }, why, sizeof(why)) != 0);
    /* truncated archive */
    memset(&z, 0, sizeof(z));
    zip_stored(&z, "eboot.elf", "hello");
    zip_finish(&z);
    z.len -= 5;
    CHECK(zip_open_text(&z, why, sizeof(why)) != 0);
    akn_fs_remove_tree(g_tmp);
}

/* ---- install, boot, rollback ------------------------------------------------------------------- */

static void write_text(const char *dir, const char *rel, const char *text) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, rel);
    char d[512];
    snprintf(d, sizeof(d), "%s", path);
    *strrchr(d, '/') = '\0';
    akn_fs_mkdirs(d);
    akn_fs_write_atomic(path, text, strlen(text));
}

static void staged_version(const char *root, const char *elf_text, char *files, size_t cap, akn_update_manifest *m,
                           const char *version) {
    memset(m, 0, sizeof(*m));
    snprintf(m->version, sizeof(m->version), "%s", version);
    char staging[320];
    CHECK(akn_update_staging(root, staging, sizeof(staging)) == 0);
    snprintf(files, cap, "%s/files", staging);
    write_text(files, "eboot.elf", elf_text);
    write_text(files, "sce_sys/icon0.png", "png");
    file_entry(m, "eboot.elf", elf_text, strlen(elf_text));
}

static void test_install(void) {
    make_tmp();
    const char *root = g_tmp;
    char why[256], files[400], path[512];
    CHECK(!akn_update_root_valid(root)); /* no homebrew.js */
    write_text(root, "homebrew.js", "// launcher");
    write_text(root, "eboot.elf", "factory 1.2.0");
    CHECK(akn_update_root_valid(root));
    CHECK(!akn_update_root_valid("relative/path"));

    akn_boot_result boot;
    CHECK(akn_update_boot(root, "1.2.0", &boot) == AKN_BOOT_NORMAL);
    CHECK_STR(boot.own_elf, "eboot.elf");

    /* install 1.2.1 from the factory 1.2.0 */
    akn_update_manifest m;
    staged_version(root, "program 1.2.1", files, sizeof(files), &m, "1.2.1");
    CHECK(akn_update_install(root, &m, files, "1.2.0", "eboot.elf", why, sizeof(why)) == 0);
    akn_update_active a;
    CHECK(akn_update_active_read(root, &a) == 0 && a.present);
    CHECK_STR(a.version, "1.2.1");
    CHECK_STR(a.elf, "update/versions/1.2.1/eboot.elf");
    CHECK(a.pending && a.attempts == 0);
    CHECK_STR(a.previous_version, "");
    CHECK_STR(a.previous_elf, "eboot.elf");
    snprintf(path, sizeof(path), "%s/update/versions/1.2.1/eboot.elf", root);
    CHECK(akn_fs_is_file(path));
    snprintf(path, sizeof(path), "%s/update/staging", root);
    CHECK(akn_fs_is_dir(path)); /* the engine wipes it; install leaves the empty parent */
    snprintf(path, sizeof(path), "%s/eboot.elf", root);
    size_t got = 0;
    char *factory = akn_fs_read_file(path, 100, &got);
    CHECK(factory && strcmp(factory, "factory 1.2.0") == 0); /* never touched */
    free(factory);

    /* first and second start of 1.2.1, then it confirms */
    CHECK(akn_update_boot(root, "1.2.1", &boot) == AKN_BOOT_TRIAL);
    CHECK(boot.attempt == 1);
    CHECK_STR(boot.own_elf, "update/versions/1.2.1/eboot.elf");
    CHECK(akn_update_confirm(root, "1.2.0") == 0); /* not the active version */
    CHECK(akn_update_confirm(root, "1.2.1") == 1);
    CHECK(akn_update_confirm(root, "1.2.1") == 0);
    akn_update_active_read(root, &a);
    CHECK(!a.pending);
    CHECK(akn_update_boot(root, "1.2.1", &boot) == AKN_BOOT_NORMAL);

    /* 1.3.0 from 1.2.1: 1.2.1 stays as the fallback */
    staged_version(root, "program 1.3.0", files, sizeof(files), &m, "1.3.0");
    CHECK(akn_update_install(root, &m, files, "1.2.1", "update/versions/1.2.1/eboot.elf", why, sizeof(why)) == 0);
    akn_update_active_read(root, &a);
    CHECK_STR(a.previous_version, "1.2.1");
    CHECK_STR(a.previous_elf, "update/versions/1.2.1/eboot.elf");
    /* the running version cannot be replaced */
    staged_version(root, "x", files, sizeof(files), &m, "1.2.1");
    CHECK(akn_update_install(root, &m, files, "1.2.1", "update/versions/1.2.1/eboot.elf", why, sizeof(why)) != 0);

    /* 1.3.0 crashes twice before confirming: the launcher starts 1.2.1, which records the rollback */
    CHECK(akn_update_boot(root, "1.3.0", &boot) == AKN_BOOT_TRIAL);
    CHECK(akn_update_boot(root, "1.3.0", &boot) == AKN_BOOT_TRIAL);
    CHECK(boot.attempt == 2);
    akn_update_active_read(root, &a);
    CHECK(a.attempts == AKN_UPDATE_MAX_ATTEMPTS);
    CHECK(akn_update_boot(root, "1.2.1", &boot) == AKN_BOOT_ROLLED_BACK);
    CHECK_STR(boot.failed_version, "1.3.0");
    CHECK_STR(boot.own_elf, "update/versions/1.2.1/eboot.elf");
    akn_update_active_read(root, &a);
    CHECK_STR(a.version, "1.2.1");
    CHECK(!a.pending);
    CHECK(akn_update_is_rejected(&a, "1.3.0"));
    snprintf(path, sizeof(path), "%s/update/versions/1.3.0", root);
    CHECK(!akn_fs_is_dir(path));
    char hist[2048];
    CHECK(akn_update_history_read(root, hist, sizeof(hist)) == 0);
    CHECK_CONTAINS(hist, "installed 1.2.1");
    CHECK_CONTAINS(hist, "rolled back from 1.3.0 to 1.2.1");

    /* a newer fixed version is installable again and clears nothing else */
    staged_version(root, "program 1.3.1", files, sizeof(files), &m, "1.3.1");
    CHECK(akn_update_install(root, &m, files, "1.2.1", "update/versions/1.2.1/eboot.elf", why, sizeof(why)) == 0);
    akn_update_active_read(root, &a);
    CHECK(akn_update_is_rejected(&a, "1.3.0"));

    /* an interrupted install leaves staging and *.tmp behind: recovery removes them */
    write_text(root, "update/staging/package.zip", "half");
    write_text(root, "update/versions/1.4.0.tmp/eboot.elf", "half");
    write_text(root, "update/active.json.tmp", "{");
    akn_update_recover(root);
    snprintf(path, sizeof(path), "%s/update/versions/1.4.0.tmp", root);
    CHECK(!akn_fs_is_dir(path));
    snprintf(path, sizeof(path), "%s/update/staging", root);
    CHECK(!akn_fs_is_dir(path));
    snprintf(path, sizeof(path), "%s/update/active.json.tmp", root);
    CHECK(!akn_fs_is_file(path));
    akn_update_active_read(root, &a);
    CHECK(a.present && strcmp(a.version, "1.3.1") == 0); /* the real pointer is intact */

    /* a damaged or hostile active.json is ignored (the launcher then starts the factory version) */
    write_text(root, "update/active.json", "{\"format\":1,\"version\":\"1.3.1\",\"elf\":\"../../evil.elf\",\"state\":\"confirmed\"}");
    CHECK(akn_update_active_read(root, &a) != 0 && !a.present);
    write_text(root, "update/active.json", "not json");
    CHECK(akn_update_active_read(root, &a) != 0 && !a.present);
    CHECK(akn_update_boot(root, "1.2.0", &boot) == AKN_BOOT_NORMAL);

    /* rollback to the factory version (no previous version entry) */
    akn_fs_remove_tree(root);
    make_tmp();
    root = g_tmp;
    write_text(root, "homebrew.js", "// launcher");
    write_text(root, "eboot.elf", "factory 1.2.0");
    staged_version(root, "program 1.2.1", files, sizeof(files), &m, "1.2.1");
    CHECK(akn_update_install(root, &m, files, "1.2.0", "eboot.elf", why, sizeof(why)) == 0);
    CHECK(akn_update_boot(root, "1.2.1", &boot) == AKN_BOOT_TRIAL);
    CHECK(akn_update_boot(root, "1.2.1", &boot) == AKN_BOOT_TRIAL);
    CHECK(akn_update_boot(root, "1.2.0", &boot) == AKN_BOOT_ROLLED_BACK);
    akn_update_active_read(root, &a);
    CHECK_STR(a.version, "1.2.0");
    CHECK_STR(a.elf, "eboot.elf");
    CHECK(akn_update_is_rejected(&a, "1.2.1"));
    akn_fs_remove_tree(root);
}

static void test_urls(void) {
    int tls = 0, port = 0;
    char host[64], target[256];
    CHECK(akn_update_split_url("https://api.github.com/repos/a/b/releases?per_page=30", &tls, host, sizeof(host), &port, target,
                               sizeof(target)) == 0);
    CHECK(tls == 1 && port == 443);
    CHECK_STR(host, "api.github.com");
    CHECK_STR(target, "/repos/a/b/releases?per_page=30");
    CHECK(akn_update_split_url("http://127.0.0.1:8443", &tls, host, sizeof(host), &port, target, sizeof(target)) == 0);
    CHECK(tls == 0 && port == 8443);
    CHECK_STR(target, "/");
    CHECK(akn_update_split_url("https://user@github.com/x", &tls, host, sizeof(host), &port, target, sizeof(target)) != 0);
    CHECK(akn_update_split_url("ftp://github.com/x", &tls, host, sizeof(host), &port, target, sizeof(target)) != 0);
    CHECK(akn_update_split_url("https://github.com/a b", &tls, host, sizeof(host), &port, target, sizeof(target)) != 0);
    CHECK(akn_update_split_url("https://github.com:0/", &tls, host, sizeof(host), &port, target, sizeof(target)) != 0);
    CHECK(akn_update_split_url("https://github.com/x\r\nHost: evil", &tls, host, sizeof(host), &port, target, sizeof(target)) != 0);
    char size[32];
    akn_update_format_size(5 * 1024 * 1024 + 300000, size, sizeof(size));
    CHECK_STR(size, "5.3 MB");
}

void run_update_tests(void) {
    test_semver();
    test_pick();
    test_manifest();
    test_signature();
    test_zip();
    test_install();
    test_urls();
}
