/*
 * AKENO NETWORK device client library.
 *
 * Talks to an AKENO NETWORK server over HTTPS and WSS using a paired-device token. It never
 * handles account passwords, never contacts any service other than the configured AKENO server,
 * and contains no console- or firmware-specific code. All network operations have timeouts,
 * all response sizes are bounded, and reconnect loops are bounded.
 */
#ifndef AKENO_AKENO_H
#define AKENO_AKENO_H

#include <stddef.h>
#include <stdint.h>

#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AKENO_CLIENT_VERSION "0.1.0"

typedef enum {
    AKENO_OK = 0,
    AKENO_ERR_ARGUMENT,        /* Invalid configuration or argument. */
    AKENO_ERR_NO_MEMORY,
    AKENO_ERR_INSECURE_URL,    /* Plain HTTP to a non-loopback host was refused. */
    AKENO_ERR_NETWORK,         /* DNS, connect or I/O failure. */
    AKENO_ERR_TIMEOUT,
    AKENO_ERR_TLS,             /* Handshake or certificate verification failed. */
    AKENO_ERR_PROTOCOL,        /* Malformed HTTP, WebSocket or JSON from the server. */
    AKENO_ERR_TOO_LARGE,       /* Response exceeded the configured limit. */
    AKENO_ERR_NOT_PAIRED,      /* No stored credential for this server. */
    AKENO_ERR_UNAUTHORIZED,    /* HTTP 401 / close 4401: token unknown, expired or revoked. */
    AKENO_ERR_FORBIDDEN,       /* HTTP 403, e.g. DEVICE_SCOPE_DENIED. */
    AKENO_ERR_NOT_FOUND,
    AKENO_ERR_RATE_LIMITED,
    AKENO_ERR_HTTP,            /* Other 4xx. */
    AKENO_ERR_SERVER,          /* 5xx. */
    AKENO_ERR_PAIRING_DENIED,
    AKENO_ERR_PAIRING_EXPIRED,
    AKENO_ERR_PAIRING_INVALID,
    AKENO_ERR_STORAGE,         /* Credential file could not be read or written safely. */
    AKENO_ERR_STOPPED,         /* The caller asked to stop. */
    AKENO_ERR_GAVE_UP          /* Reconnect attempts exhausted. */
} akeno_status;

const char *akeno_status_name(akeno_status status);

typedef enum { AKENO_LOG_DEBUG = 0, AKENO_LOG_INFO, AKENO_LOG_WARN, AKENO_LOG_ERROR } akeno_log_level;

/* Structured diagnostics: event is a stable dotted identifier, message is English text. */
typedef void (*akeno_log_fn)(void *user, akeno_log_level level, const char *event, const char *message);

/* Return non-zero to stop a blocking operation (pairing wait, realtime loop). */
typedef int (*akeno_should_stop_fn)(void *user);

typedef struct {
    const char *base_url;          /* e.g. https://akeno.example.org (required) */
    const char *ca_file;           /* PEM bundle; NULL uses the system trust store. */
    const char *credential_path;   /* Where the device token is stored (required). */
    int connect_timeout_ms;        /* Default 10000. */
    int io_timeout_ms;             /* Default 15000. */
    size_t max_response_bytes;     /* Default 1 MiB. */
    akeno_log_fn log;
    void *log_user;
    akeno_log_level log_level;     /* Default AKENO_LOG_INFO. */
} akeno_config;

typedef struct akeno_client akeno_client;

/* Global TLS initialization; call once at startup and akeno_global_cleanup() at exit. */
akeno_status akeno_global_init(void);
void akeno_global_cleanup(void);

akeno_status akeno_client_create(const akeno_config *config, akeno_client **out);
void akeno_client_destroy(akeno_client *client);

int akeno_client_is_paired(const akeno_client *client);
/* Online ID of the paired account, or NULL. */
const char *akeno_client_online_id(const akeno_client *client);

typedef struct {
    int http_status;
    char error_code[48];      /* API error code such as "DEVICE_SCOPE_DENIED", or "". */
    char error_message[256];  /* English message from the server, or "". */
    akeno_json_doc *doc;      /* Parsed body; NULL for empty bodies. */
} akeno_response;

void akeno_response_init(akeno_response *response);
void akeno_response_free(akeno_response *response);
const akeno_json *akeno_response_json(const akeno_response *response);

/*
 * Calls /api/v1<path> with the device token. json_body may be NULL. The response must be
 * released with akeno_response_free() whatever the result.
 */
akeno_status akeno_request(akeno_client *client, const char *method, const char *path, const char *json_body,
                           akeno_response *response);

/*
 * Downloads one variant of an uploaded image (profile picture or game artwork) from
 * /media/<media_id>/<file> with the device token. media_id must be 32 lowercase hex characters and
 * file one of "sm.webp", "md.webp", "lg.webp", "fallback.jpg". The body (at most max_bytes) is
 * stored in *data (malloc'd, release with free()) and the server's content type is checked to be
 * image/webp or image/jpeg and copied to content_type.
 */
akeno_status akeno_fetch_media(akeno_client *client, const char *media_id, const char *file, size_t max_bytes,
                               unsigned char **data, size_t *len, char *content_type, size_t content_type_len);

/* ---- Device pairing ---------------------------------------------------------------------- */

typedef struct {
    char device_code[64];
    char user_code[16];
    char verification_uri[320];
    char verification_uri_complete[352];
    int expires_in;
    int interval;
} akeno_pairing;

akeno_status akeno_pairing_start(akeno_client *client, const char *device_name, const char *platform,
                                 akeno_pairing *pairing, akeno_response *response);

/*
 * Polls until the user approves or denies the code, the code expires, or should_stop returns
 * non-zero. On success the credential is stored at config.credential_path (mode 0600).
 */
akeno_status akeno_pairing_wait(akeno_client *client, const akeno_pairing *pairing, akeno_should_stop_fn should_stop,
                                void *user);

/* Revokes the token on the server (best effort) and deletes the local credential. */
akeno_status akeno_logout(akeno_client *client);
/* Deletes the local credential without contacting the server. */
akeno_status akeno_forget_credentials(akeno_client *client);

/* ---- Realtime ---------------------------------------------------------------------------- */

typedef struct {
    /* Called for every server event, e.g. type "message.created". data may be NULL. */
    void (*on_event)(void *user, const char *type, const akeno_json *data);
    /* "connecting", "connected", "disconnected", "stopped"; detail is English text. */
    void (*on_state)(void *user, const char *state, const char *detail);
    akeno_should_stop_fn should_stop;
    void *user;
    int max_consecutive_failures;  /* Default 20; never infinite. */
    int backoff_base_ms;           /* Default 1000. */
    int backoff_max_ms;            /* Default 60000. */
    int heartbeat_interval_ms;     /* Default 25000 (server protocol). */
    int pong_timeout_ms;           /* Default 10000. */
} akeno_realtime_options;

/*
 * Maintains the realtime connection, reconnecting with bounded exponential backoff after
 * network failures and server restarts. Returns AKENO_ERR_STOPPED when asked to stop,
 * AKENO_ERR_UNAUTHORIZED when the device was unpaired, AKENO_ERR_FORBIDDEN without the
 * "realtime" scope, or AKENO_ERR_GAVE_UP after too many consecutive failures.
 */
akeno_status akeno_realtime_run(akeno_client *client, const akeno_realtime_options *options);

#ifdef __cplusplus
}
#endif

#endif
