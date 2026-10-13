/* Platform-specific HTTP exchange, independent from the device API and JSON semantics.
 * A console port replaces the implementation without changing client.c. */
#ifndef AKENO_HTTPCLIENT_H
#define AKENO_HTTPCLIENT_H

#include "akeno/akeno.h"
#include "buf.h"

typedef struct akeno_client akeno_client;

/* body_out must already be initialized with the caller's response size limit.
 * The backend returns the raw response bytes and HTTP status; the common client
 * handles JSON parsing, authentication and API error mapping. */
akeno_status akeno_http_backend_exchange(akeno_client *client, const char *method, const char *path,
                                        const char *json_body, int with_token, int *http_status,
                                        akeno_buf *body_out);
#endif
