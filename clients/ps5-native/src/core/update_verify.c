#include "update_verify.h"

#include <stdio.h>
#include <string.h>

#include "mbedtls/ecp.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

void akn_sha256(const void *data, size_t len, uint8_t out[32]) {
    mbedtls_sha256((const unsigned char *)data, len, out, 0);
}

int akn_sha256_file(const char *path, uint8_t out[32], long long *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    unsigned char buf[16384];
    long long total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        mbedtls_sha256_update(&ctx, buf, n);
        total += (long long)n;
    }
    int bad = ferror(f);
    fclose(f);
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
    if (size) *size = total;
    return bad ? -1 : 0;
}

int akn_update_verify_signature(const char *pem, const void *data, size_t len, const uint8_t *sig, size_t sig_len,
                                char *why, size_t why_len) {
    if (why && why_len) why[0] = '\0';
    if (!pem || !pem[0]) {
        if (why && why_len) snprintf(why, why_len, "This build has no update signing key.");
        return -1;
    }
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)pem, strlen(pem) + 1);
    if (rc != 0 || mbedtls_pk_get_type(&pk) != MBEDTLS_PK_ECKEY ||
        mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(pk)) != MBEDTLS_ECP_DP_SECP256R1) {
        if (why && why_len) snprintf(why, why_len, "The update signing key of this build is not a P-256 key (-0x%04x).", (unsigned)-rc);
        mbedtls_pk_free(&pk);
        return -1;
    }
    uint8_t digest[32];
    akn_sha256(data, len, digest);
    rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest), sig, sig_len);
    mbedtls_pk_free(&pk);
    if (rc != 0) {
        if (why && why_len) snprintf(why, why_len, "The update's signature is not valid (-0x%04x).", (unsigned)-rc);
        return -1;
    }
    return 0;
}

#include "update_key.h"

const char *akn_update_trusted_key(void) {
#ifdef AKN_UPDATE_PUBLIC_KEY_PEM
    return AKN_UPDATE_PUBLIC_KEY_PEM;
#else
    return NULL;
#endif
}
