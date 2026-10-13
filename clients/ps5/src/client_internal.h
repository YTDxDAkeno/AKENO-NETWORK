/* Internal client state shared by client.c and realtime.c. Not part of the public API. */
#ifndef AKENO_CLIENT_INTERNAL_H
#define AKENO_CLIENT_INTERNAL_H

#include "akeno/akeno.h"
#include "buf.h"
#include "url.h"

struct akeno_client {
    akeno_url url;
    char server_key[600];     /* Normalized server identity the credential is bound to. */
    char *ca_file;
    char *credential_path;
    int connect_timeout_ms;
    int io_timeout_ms;
    size_t max_response_bytes;
    akeno_log_fn log;
    void *log_user;
    akeno_log_level log_level;
    int paired;
    char token[64];
    char device_id[40];
    char online_id[40];
};

void akeno_log(const akeno_client *client, akeno_log_level level, const char *event, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;
void akeno_redact(const char *text, char *out, size_t out_len);
int akeno_build_host_header(const akeno_client *client, char *out, size_t out_len);
akeno_status akeno_request_raw(akeno_client *client, const char *method, const char *path, const char *json_body,
                               int with_token, akeno_response *response);

#endif
