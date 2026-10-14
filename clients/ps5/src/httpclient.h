/*
 * HTTP backend seam.
 *
 * The client core (pairing, credentials, JSON, capability policy) is platform-neutral and talks
 * to the server only through akeno_http_do(). There is one implementation per platform:
 *   - httpclient_posix.c  — host/Linux: the from-scratch HTTP/1.1 client (http.c) over the
 *                           POSIX + OpenSSL transport (transport_openssl.c).
 *   - httpclient_ps5.c    — PlayStation 5: the system libSceHttp2 stack (TLS via libSceSsl with
 *                           the console's own CA store).
 * Both are exercised by the same client.c, so the behavior that matters is tested on Linux.
 */
#ifndef AKENO_HTTPCLIENT_H
#define AKENO_HTTPCLIENT_H

#include "akeno/akeno.h"
#include "buf.h"

/*
 * Performs one request to <base>/api/v1<api_path>.
 *
 * - method: "GET", "POST", "PUT", "PATCH" or "DELETE".
 * - api_path: already validated by the caller (begins with '/', printable ASCII, <= 512 bytes).
 * - bearer: device token to send as `Authorization: Bearer`, or NULL.
 * - json_body: request body sent as application/json, or NULL for no body.
 * - http_status: set to the HTTP status code on success.
 * - body_out: initialized by the caller with its size limit; the backend resets and fills it
 *   with the (bounded) response body.
 *
 * Returns AKENO_OK when a complete HTTP response was received (whatever its status code), or a
 * transport/protocol error (network, timeout, TLS, protocol, too large). The backend performs
 * TLS 1.2+ with certificate and host-name verification and never disables it.
 */
akeno_status akeno_http_do(akeno_client *client, const char *method, const char *api_path, const char *bearer,
                           const char *json_body, int *http_status, akeno_buf *body_out);

#endif
