/*
 * The signed-in AKENO account as the native app sees it, built from read-only sources:
 *   - the daemon's settings file (server address) and credential file (presence and mode only;
 *     the token itself is read by the client library and never touches this module),
 *   - GET /api/v1/devices/current (existing endpoint, device token),
 *   - the daemon's GET /api/instance liveness answer on 127.0.0.1:8642.
 * Parsing and state are portable and unit-tested; the network calls live in app/net.c.
 */
#ifndef AKN_ACCOUNT_H
#define AKN_ACCOUNT_H

#include <stddef.h>

typedef enum {
    AKN_ACC_UNCHECKED = 0,
    AKN_ACC_CHECKING,
    AKN_ACC_NO_SERVER,     /* settings.json has no usable server */
    AKN_ACC_NOT_PAIRED,    /* no credential for this server */
    AKN_ACC_SIGNED_IN,
    AKN_ACC_UNAUTHORIZED,  /* 401: token revoked or expired */
    AKN_ACC_FORBIDDEN,     /* 403 */
    AKN_ACC_OFFLINE,       /* DNS/connect/timeout */
    AKN_ACC_TLS,           /* certificate or handshake */
    AKN_ACC_SERVER_ERROR,  /* 5xx or malformed answer */
    AKN_ACC_ERROR          /* anything else */
} akn_account_state;

typedef enum { AKN_DAEMON_UNCHECKED = 0, AKN_DAEMON_RUNNING, AKN_DAEMON_NOT_RUNNING, AKN_DAEMON_OTHER } akn_daemon_state;

typedef struct {
    akn_account_state state;
    const char *server_desc;   /* akn_describe_server_url(): never the host */
    int credential_present;
    int credential_private;    /* mode 0600 (group/other have no access) */
    char online_id[40];
    char display_name[80];
    char device_name[80];
    int scope_count;
    char scopes[400];          /* comma-separated, as granted on the website */
    int has_realtime_scope;
    long long clock_skew_s;    /* server time - console time */
    int have_skew;
    int http_status;
    char error_code[48];
    char status_name[32];      /* akeno_status_name() of the last request */
    unsigned duration_ms;
    unsigned long checks;

    akn_daemon_state daemon;
    char daemon_version[32];
    int daemon_ipc; /* native IPC protocol of the running daemon, 0 = none */
} akn_account_view;

/*
 * Reads "server" from the daemon's settings file without modifying it. Returns 0 and fills out
 * (possibly with "") on success, -1 if the file is missing or invalid.
 */
int akn_read_server_setting(const char *settings_path, char *out, size_t len);

/* Inspects the credential file (existence, regular file, permissions) without reading it. */
void akn_inspect_credential(const char *credential_path, akn_account_view *view);

/*
 * Reads the native-IPC daemon's per-launch key (docs/PS5_NATIVE_PHASE2.md): it must be a regular
 * file, not a link, readable by its owner only, holding exactly 64 hex digits. Returns 0, -1 when
 * the file does not exist, -2 when it is unsafe or malformed (nothing is copied then).
 */
#define AKN_NATIVE_KEY_LEN 64
int akn_read_native_key(const char *path, char *out, size_t len);

struct akeno_json;
/* Fills the account fields from a GET /devices/current answer. Returns 0, or -1 if malformed. */
int akn_fill_device_self(const struct akeno_json *root, long long console_unix_now, akn_account_view *view);
int akn_parse_device_self(const char *json, size_t len, long long console_unix_now, akn_account_view *view);

/*
 * Checks a GET /api/instance answer: 0 and the version when it is the AKENO console daemon,
 * -1 for any other answer. *native_ipc (may be NULL) is the daemon's native IPC protocol version,
 * 0 for a daemon without the native interface (the Alpha 0.5 release).
 */
int akn_parse_daemon_instance(const char *json, size_t len, char *version, size_t version_len, int *native_ipc);

/* "2026-10-17T12:34:56.789Z" -> seconds since 1970 (UTC). Returns 0, or -1 if malformed. */
int akn_parse_iso8601(const char *s, long long *out);

const char *akn_account_state_text(akn_account_state state);
const char *akn_daemon_state_text(akn_daemon_state state);

#endif
