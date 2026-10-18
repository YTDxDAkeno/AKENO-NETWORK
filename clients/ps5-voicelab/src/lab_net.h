/* Voice Lab network probes (POSIX sockets; the same code on the PS5 and the PC). */
#ifndef AKN_LAB_NET_H
#define AKN_LAB_NET_H

#include <stddef.h>

/* GET /api/instance on the AKENO daemon's local port; 0 when it answers HTTP. */
int akn_lab_probe_http(const char *host, int port, const char *path, int timeout_ms, int *ms);

/* A TCP connection to host:port (DNS included); 0 when it connects. */
int akn_lab_probe_tcp(const char *host, int port, int timeout_ms, int *ms);

/* Reads "server" from the daemon's settings.json (read only) and splits it into host and port.
 * Returns 0, or -1 when there is no usable server address. */
int akn_lab_server_from_settings(const char *path, char *host, size_t host_len, int *port);

#endif
