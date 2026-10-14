/*
 * PlayStation 5 platform helpers: time, sleep, randomness, and global init/cleanup.
 *
 * Networking on PS5 goes through libSceHttp2 (httpclient_ps5.c), not the byte-stream transport
 * used on the host, so the akeno_conn_* functions are intentionally absent here; the PS5 build
 * does not compile http.c, ws.c or realtime.c.
 */
#include <stdlib.h>
#include <time.h>

#include "../crypto_util.h"
#include "../transport.h"
#include "ps5_platform.h"

/* Provided by the C library; present in the ps5-payload SDK libc. */
void arc4random_buf(void *buf, size_t n);

akeno_status akeno_transport_init(void) {
    return akeno_ps5_http_global_init();
}

void akeno_transport_cleanup(void) {
    akeno_ps5_http_global_fini();
}

long long akeno_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void akeno_sleep_ms(int ms) {
    if (ms <= 0) return;
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int akeno_random_bytes(void *out, size_t len) {
    arc4random_buf(out, len);
    return 0;
}
