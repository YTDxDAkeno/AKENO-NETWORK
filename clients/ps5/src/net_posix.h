/*
 * BSD socket helpers shared by the TLS transports (OpenSSL on Linux, mbedTLS on Linux and the
 * PS5). Works with glibc and with the FreeBSD-derived libc of the PS5 payload SDK, whose
 * getaddrinfo() resolves IPv4 only (through libSceNet).
 */
#ifndef AKENO_NET_POSIX_H
#define AKENO_NET_POSIX_H

#include <stddef.h>

#include "akeno/akeno.h"
#include "url.h"

/* Opens a non-blocking TCP connection before the deadline (akeno_now_ms() based). */
akeno_status akeno_tcp_connect(const akeno_url *url, long long deadline, int *out_fd, char *err, size_t err_len);
/* Waits for POLLIN/POLLOUT; returns 1 ready, 0 timeout, -1 error. */
int akeno_wait_fd(int fd, short events, int timeout_ms);
/* Milliseconds left until deadline, clamped to [0, INT_MAX]. */
int akeno_remaining_ms(long long deadline);
/* Sends without raising SIGPIPE; same return convention as send(). */
long akeno_send_nosignal(int fd, const void *buf, size_t len);

#endif
