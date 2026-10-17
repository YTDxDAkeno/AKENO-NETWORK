#define _POSIX_C_SOURCE 200809L

#include "account.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "akeno/json.h"

#define SETTINGS_MAX_BYTES 4096

static void copy_str(char *dst, size_t len, const char *src) { snprintf(dst, len, "%s", src ? src : ""); }

int akn_read_server_setting(const char *settings_path, char *out, size_t len) {
    out[0] = '\0';
    int flags = O_RDONLY;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(settings_path, flags);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > SETTINGS_MAX_BYTES) {
        close(fd);
        return -1;
    }
    char data[SETTINGS_MAX_BYTES];
    ssize_t n = read(fd, data, sizeof(data));
    close(fd);
    if (n <= 0) return -1;
    akeno_json_doc *doc = akeno_json_parse(data, (size_t)n, NULL, 0);
    if (!doc) return -1;
    const char *server = akeno_json_get_string(akeno_json_root(doc), "server", "");
    int ok = strlen(server) < len;
    if (ok) copy_str(out, len, server);
    akeno_json_free(doc);
    return ok ? 0 : -1;
}

void akn_inspect_credential(const char *credential_path, akn_account_view *view) {
    struct stat st;
    view->credential_present = 0;
    view->credential_private = 0;
    if (lstat(credential_path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) return;
    view->credential_present = 1;
    view->credential_private = (st.st_mode & 077) == 0;
}

static int digits(const char *s, int n, int *out) {
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (!isdigit((unsigned char)s[i])) return -1;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 0;
}

/* Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm). */
static long long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

int akn_parse_iso8601(const char *s, long long *out) {
    int y, mo, d, h, mi, se;
    if (!s || strlen(s) < 20) return -1;
    if (digits(s, 4, &y) || s[4] != '-' || digits(s + 5, 2, &mo) || s[7] != '-' || digits(s + 8, 2, &d) || s[10] != 'T' ||
        digits(s + 11, 2, &h) || s[13] != ':' || digits(s + 14, 2, &mi) || s[16] != ':' || digits(s + 17, 2, &se))
        return -1;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return -1;
    const char *p = s + 19;
    if (*p == '.') {
        p++;
        if (!isdigit((unsigned char)*p)) return -1;
        while (isdigit((unsigned char)*p)) p++;
    }
    if (*p != 'Z' || p[1] != '\0') return -1;
    *out = days_from_civil(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL + se;
    return 0;
}

int akn_fill_device_self(const akeno_json *root, long long console_unix_now, akn_account_view *view) {
    const akeno_json *user = akeno_json_get(root, "user");
    const akeno_json *device = akeno_json_get(root, "device");
    const char *online_id = akeno_json_get_string(user, "onlineId", "");
    if (!user || !device || !online_id[0]) return -1;
    copy_str(view->online_id, sizeof(view->online_id), online_id);
    copy_str(view->display_name, sizeof(view->display_name), akeno_json_get_string(user, "displayName", ""));
    copy_str(view->device_name, sizeof(view->device_name), akeno_json_get_string(device, "name", ""));
    view->scopes[0] = '\0';
    view->scope_count = 0;
    view->has_realtime_scope = 0;
    const akeno_json *scopes = akeno_json_get(device, "scopes");
    for (size_t i = 0; scopes && scopes->type == AKENO_JSON_ARRAY && i < scopes->count; i++) {
        const char *scope = akeno_json_string(akeno_json_at(scopes, i), NULL);
        if (!scope) continue;
        size_t used = strlen(view->scopes);
        snprintf(view->scopes + used, sizeof(view->scopes) - used, "%s%s", used ? ", " : "", scope);
        view->scope_count++;
        if (strcmp(scope, "realtime") == 0) view->has_realtime_scope = 1;
    }
    long long server_now;
    view->have_skew = 0;
    if (console_unix_now > 0 && akn_parse_iso8601(akeno_json_get_string(root, "serverTime", ""), &server_now) == 0) {
        view->clock_skew_s = server_now - console_unix_now;
        view->have_skew = 1;
    }
    return 0;
}

int akn_parse_device_self(const char *json, size_t len, long long console_unix_now, akn_account_view *view) {
    akeno_json_doc *doc = akeno_json_parse(json, len, NULL, 0);
    if (!doc) return -1;
    int rc = akn_fill_device_self(akeno_json_root(doc), console_unix_now, view);
    akeno_json_free(doc);
    return rc;
}

int akn_parse_daemon_instance(const char *json, size_t len, char *version, size_t version_len) {
    akeno_json_doc *doc = akeno_json_parse(json, len, NULL, 0);
    if (!doc) return -1;
    const akeno_json *root = akeno_json_root(doc);
    int rc = -1;
    if (strcmp(akeno_json_get_string(root, "app", ""), "akeno-network-console") == 0) {
        copy_str(version, version_len, akeno_json_get_string(root, "version", "?"));
        rc = 0;
    }
    akeno_json_free(doc);
    return rc;
}

const char *akn_account_state_text(akn_account_state state) {
    switch (state) {
    case AKN_ACC_UNCHECKED: return "Not checked yet";
    case AKN_ACC_CHECKING: return "Checking...";
    case AKN_ACC_NO_SERVER: return "No server configured";
    case AKN_ACC_NOT_PAIRED: return "This console is not paired";
    case AKN_ACC_SIGNED_IN: return "Signed in";
    case AKN_ACC_UNAUTHORIZED: return "Pairing no longer accepted";
    case AKN_ACC_FORBIDDEN: return "Permission denied";
    case AKN_ACC_OFFLINE: return "Server unreachable";
    case AKN_ACC_TLS: return "Secure connection failed";
    case AKN_ACC_SERVER_ERROR: return "Server error";
    case AKN_ACC_ERROR: break;
    }
    return "Error";
}

const char *akn_daemon_state_text(akn_daemon_state state) {
    switch (state) {
    case AKN_DAEMON_UNCHECKED: return "Not checked yet";
    case AKN_DAEMON_RUNNING: return "Running";
    case AKN_DAEMON_NOT_RUNNING: return "Not running";
    case AKN_DAEMON_OTHER: return "Port 8642 is used by another program";
    }
    return "?";
}
