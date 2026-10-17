/*
 * AKENO NETWORK console app: lifecycle, settings, logging, realtime and pairing workers, native
 * notifications and the event feed for the local UI.
 */
#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "app_internal.h"
#ifdef AKENO_EXPERIMENTAL_AUDIO
#include "audio_lab.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "client_internal.h"
#include "crypto_util.h"
#include "json_write.h"
#include "transport.h"
#include "ui_http.h"

#define LOG_ROTATE_BYTES (512 * 1024)
#define SETTINGS_MAX_BYTES 8192
#define NOTIFY_BURST 5.0
#define NOTIFY_REFILL_MS 3000

static volatile sig_atomic_t g_exit_requested = 0;

void akeno_app_request_exit(void) { g_exit_requested = 1; }

/* ---- Logging ------------------------------------------------------------------------------ */

static const char *const LEVEL_NAMES[] = { "debug", "info", "warn", "error" };

static void log_write(akeno_app *app, akeno_log_level level, const char *event, const char *message) {
    char clean[400];
    akeno_redact(message, clean, sizeof(clean));
    for (char *p = clean; *p; p++)
        if ((unsigned char)*p < 0x20) *p = ' ';
    time_t now = time(NULL);
    struct tm tm;
    char stamp[32] = "";
    if (gmtime_r(&now, &tm)) strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm);
    char line[520];
    snprintf(line, sizeof(line), "%s %-5s %s %s", stamp, LEVEL_NAMES[level], event, clean);
    pthread_mutex_lock(&app->log_lock);
    if (app->log) {
        fprintf(app->log, "%s\n", line);
        fflush(app->log);
    }
    size_t keep = strlen(line) < sizeof(app->log_ring[0]) ? strlen(line) : sizeof(app->log_ring[0]) - 1;
    memcpy(app->log_ring[app->log_next], line, keep); /* Long lines are shortened in the ring only. */
    app->log_ring[app->log_next][keep] = '\0';
    app->log_next = (app->log_next + 1) % APP_LOG_RING;
    if (app->log_count < APP_LOG_RING) app->log_count++;
    pthread_mutex_unlock(&app->log_lock);
    akeno_platform_debug_line(line);
}

void app_log(akeno_app *app, akeno_log_level level, const char *event, const char *fmt, ...) {
    char message[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    log_write(app, level, event, message);
}

/* Library diagnostics arrive already redacted. */
static void library_log(void *user, akeno_log_level level, const char *event, const char *message) {
    log_write(user, level, event, message);
}

static void open_log(akeno_app *app) {
    struct stat st;
    if (stat(app->log_path, &st) == 0 && st.st_size > LOG_ROTATE_BYTES) {
        char old[620];
        snprintf(old, sizeof(old), "%s.1", app->log_path);
        rename(app->log_path, old);
    }
    int fd = open(app->log_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    app->log = fd >= 0 ? fdopen(fd, "a") : NULL;
    if (fd >= 0 && !app->log) close(fd);
}

/* ---- Small helpers ------------------------------------------------------------------------ */

void app_copy_str(char *dst, size_t dst_len, const char *src) { snprintf(dst, dst_len, "%s", src ? src : ""); }

static void random_hex(akeno_app *app, char *out, size_t bytes) {
    unsigned char raw[32];
    static const char hex[] = "0123456789abcdef";
    if (bytes > sizeof(raw) || akeno_random_bytes(raw, bytes) != 0) {
        app_log(app, AKENO_LOG_ERROR, "app.random_failed", "No secure random numbers are available.");
        abort(); /* Never run the local UI with a guessable secret. */
    }
    for (size_t i = 0; i < bytes; i++) {
        out[i * 2] = hex[raw[i] >> 4];
        out[i * 2 + 1] = hex[raw[i] & 15];
    }
    out[bytes * 2] = '\0';
    memset(raw, 0, sizeof(raw));
}

/* Truncates UTF-8 text to at most max bytes without splitting a character. */
static void utf8_truncate(char *s, size_t max) {
    size_t len = strlen(s);
    if (len <= max) return;
    size_t cut = max;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
    s[cut] = '\0';
}

static void sanitize_line(char *s) {
    for (unsigned char *p = (unsigned char *)s; *p; p++)
        if (*p < 0x20 || *p == 0x7F) *p = ' ';
}

/* Writes data atomically with mode 0600. */
static int write_private_file(const char *path, const char *data, size_t len) {
    char tmp[700];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return -1;
    int ok = write(fd, data, len) == (ssize_t)len && fsync(fd) == 0;
    ok = close(fd) == 0 && ok;
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* ---- Settings ----------------------------------------------------------------------------- */

static void load_settings(akeno_app *app) {
    app_copy_str(app->device_name, sizeof(app->device_name), "PlayStation 5");
    app->notify_messages = 1;
    app->notify_social = 1;
    app->activity_enabled = 1;
    app->actionable_notifications = 0;
    app->qa_shortcut = 0;
    app_copy_str(app->qa_combo, sizeof(app->qa_combo), akeno_shortcut_default_combo()->id);
    app->qa_hold_ms = 1000;
    int fd = open(app->settings_path, O_RDONLY);
    if (fd < 0) return;
    char data[SETTINGS_MAX_BYTES];
    ssize_t n = read(fd, data, sizeof(data));
    close(fd);
    akeno_json_doc *doc = n > 0 ? akeno_json_parse(data, (size_t)n, NULL, 0) : NULL;
    if (!doc) {
        app_log(app, AKENO_LOG_WARN, "settings.invalid", "Ignoring an unreadable settings file.");
        return;
    }
    const akeno_json *root = akeno_json_root(doc);
    app_copy_str(app->server, sizeof(app->server), akeno_json_get_string(root, "server", ""));
    const char *name = akeno_json_get_string(root, "deviceName", "");
    if (name[0]) app_copy_str(app->device_name, sizeof(app->device_name), name);
    app->notify_messages = akeno_json_get_bool(root, "notifyMessages", 1);
    app->notify_social = akeno_json_get_bool(root, "notifySocial", 1);
    app->activity_enabled = akeno_json_get_bool(root, "activityEnabled", 1);
    app->actionable_notifications = akeno_json_get_bool(root, "actionableNotifications", 0);
    app->qa_shortcut = akeno_json_get_bool(root, "quickAccessShortcut", 0);
    const char *combo = akeno_json_get_string(root, "quickAccessCombo", "");
    if (akeno_shortcut_combo_find(combo)) app_copy_str(app->qa_combo, sizeof(app->qa_combo), combo);
    int64_t hold = 0;
    if (akeno_json_int64(akeno_json_get(root, "quickAccessHoldMs"), &hold) == 0 && akeno_shortcut_hold_valid((int)hold)) app->qa_hold_ms = (int)hold;
    akeno_json_free(doc);
}

akeno_status app_save_settings(akeno_app *app) {
    akeno_buf out;
    akeno_buf_init(&out, SETTINGS_MAX_BYTES);
    pthread_mutex_lock(&app->lock);
    int rc = akeno_buf_append_str(&out, "{\"version\":1,\"server\":");
    rc |= akeno_buf_append_json_string(&out, app->server);
    rc |= akeno_buf_append_str(&out, ",\"deviceName\":");
    rc |= akeno_buf_append_json_string(&out, app->device_name);
    rc |= akeno_buf_printf(&out, ",\"notifyMessages\":%s,\"notifySocial\":%s,\"activityEnabled\":%s,\"actionableNotifications\":%s",
                           app->notify_messages ? "true" : "false", app->notify_social ? "true" : "false",
                           app->activity_enabled ? "true" : "false", app->actionable_notifications ? "true" : "false");
    rc |= akeno_buf_printf(&out, ",\"quickAccessShortcut\":%s,\"quickAccessCombo\":", app->qa_shortcut ? "true" : "false");
    rc |= akeno_buf_append_json_string(&out, app->qa_combo);
    rc |= akeno_buf_printf(&out, ",\"quickAccessHoldMs\":%d}\n", app->qa_hold_ms);
    pthread_mutex_unlock(&app->lock);
    akeno_status status = AKENO_ERR_NO_MEMORY;
    if (rc == 0) status = write_private_file(app->settings_path, out.data, out.len) == 0 ? AKENO_OK : AKENO_ERR_STORAGE;
    akeno_buf_free(&out);
    if (status != AKENO_OK) app_log(app, AKENO_LOG_ERROR, "settings.write_failed", "Could not save the settings.");
    return status;
}

/* ---- Clients ------------------------------------------------------------------------------ */

akeno_status app_make_client(akeno_app *app, akeno_client **out) {
    char server[300];
    pthread_mutex_lock(&app->lock);
    app_copy_str(server, sizeof(server), app->server);
    pthread_mutex_unlock(&app->lock);
    *out = NULL;
    if (!server[0]) return AKENO_ERR_ARGUMENT;
    akeno_config config = { 0 };
    config.base_url = server;
    config.ca_file = app->ca_file;
    config.credential_path = app->credential_path;
    config.io_timeout_ms = app->opt.io_timeout_ms;
    config.connect_timeout_ms = app->opt.io_timeout_ms;
    config.log = library_log;
    config.log_user = app;
    config.log_level = AKENO_LOG_INFO;
    return akeno_client_create(&config, out);
}

/* Main thread: (re)creates the UI client from the stored credential and publishes the status. */
static void reload_ui_client(akeno_app *app) {
    akeno_client_destroy(app->ui_client);
    app->ui_client = NULL;
    akeno_status status = app_make_client(app, &app->ui_client);
    pthread_mutex_lock(&app->lock);
    app->paired = status == AKENO_OK && akeno_client_is_paired(app->ui_client);
    const char *online_id = app->paired ? akeno_client_online_id(app->ui_client) : NULL;
    app_copy_str(app->online_id, sizeof(app->online_id), online_id);
    if (app->paired) {
        app->rt_wanted = 1;
        app->act_wanted = 1;
    }
    pthread_mutex_unlock(&app->lock);
    if (status != AKENO_OK && status != AKENO_ERR_ARGUMENT)
        app_log(app, AKENO_LOG_WARN, "app.client_failed", "The server address cannot be used (%s).", akeno_status_name(status));
}

void app_mark_unpaired(akeno_app *app, const char *why) {
    if (app->ui_client) akeno_forget_credentials(app->ui_client);
    else unlink(app->credential_path);
    pthread_mutex_lock(&app->lock);
    int was_paired = app->paired;
    app->paired = 0;
    app->online_id[0] = '\0';
    app->credentials_changed = 1;
    app->rt_stop = 1;
    pthread_mutex_unlock(&app->lock);
    if (was_paired) app_log(app, AKENO_LOG_WARN, "app.unpaired", "%s", why);
}

/* ---- Events and notifications ------------------------------------------------------------- */

void app_push_event(akeno_app *app, const char *type, const char *data_json) {
    akeno_buf tail;
    akeno_buf_init(&tail, APP_EVENT_MAX_BYTES);
    int rc = akeno_buf_append_str(&tail, ",\"type\":");
    rc |= akeno_buf_append_json_string(&tail, type);
    rc |= akeno_buf_append_str(&tail, ",\"data\":");
    if (rc == 0 && akeno_buf_append_str(&tail, data_json ? data_json : "null") != 0) {
        /* Too large for the feed: keep the type so the UI refreshes through the API. */
        akeno_buf_reset(&tail);
        rc = akeno_buf_append_str(&tail, ",\"type\":");
        rc |= akeno_buf_append_json_string(&tail, type);
        rc |= akeno_buf_append_str(&tail, ",\"data\":null");
    }
    rc |= akeno_buf_append_str(&tail, "}");
    if (rc != 0) {
        akeno_buf_free(&tail);
        return;
    }
    pthread_mutex_lock(&app->lock);
    unsigned long long seq = ++app->next_seq;
    size_t size = tail.len + 32;
    char *json = malloc(size);
    if (json) {
        snprintf(json, size, "{\"seq\":%llu%s", seq, tail.data);
        app_event *slot = &app->events[seq % APP_EVENT_RING];
        free(slot->json);
        slot->seq = seq;
        slot->json = json;
    }
    pthread_mutex_unlock(&app->lock);
    akeno_buf_free(&tail);
}

/* Token bucket shared by all notifications. Returns 1 when one may be shown now. */
static int notify_allowed(akeno_app *app, int social) {
    pthread_mutex_lock(&app->lock);
    int enabled = social ? app->notify_social : app->notify_messages;
    long long now = akeno_now_ms();
    app->notify_tokens += (double)(now - app->notify_refill_at) / NOTIFY_REFILL_MS;
    if (app->notify_tokens > NOTIFY_BURST) app->notify_tokens = NOTIFY_BURST;
    app->notify_refill_at = now;
    int allowed = enabled && app->notify_tokens >= 1.0;
    if (allowed) app->notify_tokens -= 1.0;
    else if (enabled) app->notifications_dropped++;
    pthread_mutex_unlock(&app->lock);
    return allowed;
}

static void clean_text(char *dst, size_t dst_len, const char *src, size_t max_bytes) {
    app_copy_str(dst, dst_len, src);
    sanitize_line(dst);
    utf8_truncate(dst, max_bytes);
}

/* The proven plain notification (sceKernelSendNotificationRequest on the PS5). */
static void send_plain(akeno_app *app, const char *text) {
    char clean[240];
    clean_text(clean, sizeof(clean), text, 200);
    int rc = akeno_platform_notify(clean);
    pthread_mutex_lock(&app->lock);
    if (rc == 0) app->notifications_sent++;
    else app->notifications_failed++;
    pthread_mutex_unlock(&app->lock);
    if (rc != 0) app_log(app, AKENO_LOG_WARN, "notify.failed", "The system did not accept a notification.");
}

void app_notify(akeno_app *app, int social, const char *text) {
    if (notify_allowed(app, social)) send_plain(app, text);
}

/*
 * Tries the experimental button notification (only when turned on in the settings). Returns the
 * platform result: 0 shown, -1 refused, -2 unavailable, -3 no ticket. Never shows a plain one.
 */
static int send_actionable(akeno_app *app, const char *title, const char *body, const char *button, const char *open) {
    char url[160];
    if (app_ticket_url(app, open, url, sizeof(url)) != 0) return -3;
    char t[160], b[240], label[60];
    clean_text(t, sizeof(t), title, 100);
    clean_text(b, sizeof(b), body, 200);
    clean_text(label, sizeof(label), button, 40);
    int rc = akeno_platform_notify_action(t, b, label, url);
    pthread_mutex_lock(&app->lock);
    int first_problem = rc != 0 && app->actionable_failed + app->actionable_unavailable == 0;
    if (rc == 0) {
        app->actionable_sent++;
        app->notifications_sent++;
    } else if (rc == -2) {
        app->actionable_unavailable++;
    } else {
        app->actionable_failed++;
    }
    pthread_mutex_unlock(&app->lock);
    if (first_problem)
        app_log(app, AKENO_LOG_WARN, "notify.actionable_failed", "Button notification %s (%s); plain notifications are used instead.",
                rc == -2 ? "unavailable" : "refused", akeno_platform_actionable_status());
    return rc;
}

/* A message notification: with a button when enabled and working, otherwise the proven plain one. */
static void notify_message(akeno_app *app, const char *plain, const char *title, const char *body, const char *open) {
    if (!notify_allowed(app, 0)) return;
    pthread_mutex_lock(&app->lock);
    int actionable = app->actionable_notifications;
    pthread_mutex_unlock(&app->lock);
    if (actionable && send_actionable(app, title, body, "Open in AKENO", open) == 0) return;
    send_plain(app, plain);
}

int app_test_actionable(akeno_app *app) {
    int rc = send_actionable(app, "AKENO NETWORK test", "Select the button to open AKENO. If nothing appears, turn button notifications off.",
                             "Open AKENO", "messages");
    app_log(app, rc == 0 ? AKENO_LOG_INFO : AKENO_LOG_WARN, "notify.actionable_test", "Test button notification: result %d (%s).", rc,
            akeno_platform_actionable_status());
    return rc;
}

/* ---- Quick access: page targets, tickets and recent messages ------------------------------- */

static int is_uuid(const char *s) {
    if (strlen(s) != 36) return 0;
    for (int i = 0; i < 36; i++) {
        char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return 0;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return 0;
        }
    }
    return 1;
}

int app_valid_open(const char *target) {
    if (!target) return 0;
    static const char *const plain[] = { "", "messages", "party", "friends", "games", "invitations", "profile" };
    for (size_t i = 0; i < sizeof(plain) / sizeof(plain[0]); i++)
        if (strcmp(target, plain[i]) == 0) return 1;
    if (strncmp(target, "profile:", 8) == 0) return is_uuid(target + 8);
    return strncmp(target, "conversation:", 13) == 0 && is_uuid(target + 13);
}

int app_ticket_url(akeno_app *app, const char *open, char *url, size_t url_len) {
    if (!app_valid_open(open) || app->port <= 0) return -1;
    unsigned char raw[16];
    if (akeno_random_bytes(raw, sizeof(raw)) != 0) return -1;
    char id[33];
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(raw); i++) {
        id[i * 2] = hex[raw[i] >> 4];
        id[i * 2 + 1] = hex[raw[i] & 15];
    }
    id[32] = '\0';
    memset(raw, 0, sizeof(raw));
    long long now = akeno_now_ms();
    pthread_mutex_lock(&app->lock);
    app_ticket *slot = &app->tickets[0];
    for (int i = 0; i < APP_TICKETS; i++) {
        app_ticket *t = &app->tickets[i];
        if (!t->id[0] || t->expires_at <= now) {
            slot = t;
            break;
        }
        if (t->expires_at < slot->expires_at) slot = t; /* All in use: replace the oldest. */
    }
    memcpy(slot->id, id, sizeof(slot->id));
    app_copy_str(slot->open, sizeof(slot->open), open);
    slot->expires_at = now + (app->opt.ticket_ms > 0 ? app->opt.ticket_ms : APP_TICKET_MS);
    pthread_mutex_unlock(&app->lock);
    int n = snprintf(url, url_len, "http://127.0.0.1:%d/t/%s", app->port, id);
    memset(id, 0, sizeof(id));
    return n > 0 && (size_t)n < url_len ? 0 : -1;
}

int app_ticket_redeem(akeno_app *app, const char *id, char *open, size_t open_len) {
    if (open_len) open[0] = '\0';
    if (!id || strlen(id) != 32) return -1;
    for (int i = 0; i < 32; i++)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return -1;
    long long now = akeno_now_ms();
    int rc = -1;
    pthread_mutex_lock(&app->lock);
    for (int i = 0; i < APP_TICKETS; i++) {
        app_ticket *t = &app->tickets[i];
        if (!t->id[0] || !ui_http_secret_equal(t->id, id)) continue;
        if (t->expires_at > now) {
            app_copy_str(open, open_len, t->open);
            rc = 0;
        }
        memset(t, 0, sizeof(*t)); /* Single use, expired or not. */
    }
    pthread_mutex_unlock(&app->lock);
    return rc;
}

/* Records a message from someone else for the page's quick-access list. */
static void remember_message(akeno_app *app, const char *open, const char *kind, const char *from, const char *body) {
    pthread_mutex_lock(&app->lock);
    unsigned long long seq = ++app->recent_seq;
    app_recent_message *m = &app->recent[seq % APP_RECENT_MESSAGES];
    memset(m, 0, sizeof(*m));
    m->seq = seq;
    m->at = akeno_now_ms();
    app_copy_str(m->open, sizeof(m->open), open);
    app_copy_str(m->kind, sizeof(m->kind), kind);
    clean_text(m->from, sizeof(m->from), from, 64);
    clean_text(m->preview, sizeof(m->preview), body, 120);
    pthread_mutex_unlock(&app->lock);
}

void app_latest_message_target(akeno_app *app, char *out, size_t out_len) {
    pthread_mutex_lock(&app->lock);
    const app_recent_message *m = app->recent_seq ? &app->recent[app->recent_seq % APP_RECENT_MESSAGES] : NULL;
    app_copy_str(out, out_len, m && m->seq == app->recent_seq && m->seq > app->recent_seen_seq ? m->open : "");
    pthread_mutex_unlock(&app->lock);
}

/* message.created from someone else: remember it and notify (with a button when enabled). */
static void on_message(akeno_app *app, const char *self, const akeno_json *data) {
    const akeno_json *message = akeno_json_get(data, "message");
    const akeno_json *sender = akeno_json_get(message, "sender");
    const char *online_id = akeno_json_get_string(sender, "onlineId", "");
    if (!online_id[0] || strcmp(online_id, self) == 0) return;
    const char *name = akeno_json_get_string(sender, "displayName", "");
    if (!name[0]) name = online_id;
    const char *kind = akeno_json_get_string(data, "conversationKind", "direct");
    const char *conversation = akeno_json_get_string(data, "conversationId", "");
    const char *body = akeno_json_get_string(message, "body", "");
    char open[APP_OPEN_MAX] = "messages";
    if (strcmp(kind, "party") == 0) app_copy_str(open, sizeof(open), "party");
    else if (strcmp(kind, "direct") == 0 && is_uuid(conversation)) snprintf(open, sizeof(open), "conversation:%s", conversation);
    remember_message(app, open, kind, name, body);

    const char *where = strcmp(kind, "party") == 0 ? " (party)" : strcmp(kind, "group") == 0 ? " (group)" : "";
    char plain[400], title[160];
    snprintf(plain, sizeof(plain), "AKENO: %s%s: %s", name, where, body);
    snprintf(title, sizeof(title), "AKENO: %s%s", name, where);
    notify_message(app, plain, title, body, open);
}

/* Builds the notification text for a realtime event, or returns 0 if it does not warrant one. */
static int notification_text(const char *type, const akeno_json *data, char *out, size_t out_len, int *social) {
    *social = 1;
    if (strcmp(type, "notification.created") != 0) return 0;
    const akeno_json *n = akeno_json_path(data, "notification.data");
    const char *kind = akeno_json_get_string(n, "type", "");
    const akeno_json *actor = akeno_json_get(n, "actor");
    const char *who = akeno_json_get_string(actor, "displayName", "");
    if (!who[0]) who = akeno_json_get_string(actor, "onlineId", "Someone");
    if (strcmp(kind, "friend_request.received") == 0) {
        snprintf(out, out_len, "AKENO: %s sent you a friend request.", who);
    } else if (strcmp(kind, "friend_request.accepted") == 0) {
        snprintf(out, out_len, "AKENO: %s accepted your friend request.", who);
    } else if (strcmp(kind, "invitation.received") == 0) {
        const char *what = akeno_json_get_string(n, "kind", "");
        const char *label = strcmp(what, "party") == 0 ? "the party" : strcmp(what, "game_session") == 0 ? "the game session" : "the group";
        snprintf(out, out_len, "AKENO: %s invited you to %s \"%s\".", who, label, akeno_json_get_string(n, "targetName", ""));
    } else if (strcmp(kind, "game_session.join_requested") == 0) {
        snprintf(out, out_len, "AKENO: %s asked to join \"%s\".", who, akeno_json_get_string(n, "gameName", ""));
    } else if (strcmp(kind, "game_session.request_accepted") == 0) {
        snprintf(out, out_len, "AKENO: %s accepted you into \"%s\".", who, akeno_json_get_string(n, "gameName", ""));
    } else {
        return 0;
    }
    return 1;
}

/* ---- Worker threads ----------------------------------------------------------------------- */

/* Workers run TLS handshakes; give them an explicit stack instead of the platform default,
 * which is not documented for PS5 payloads. */
#define WORKER_STACK_BYTES ((size_t)1 << 20)

int app_start_worker(pthread_t *thread, void *(*fn)(void *), void *arg) {
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0) return -1;
    int rc = pthread_attr_setstacksize(&attr, WORKER_STACK_BYTES);
    if (rc == 0) rc = pthread_create(thread, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    return rc;
}

/* ---- Realtime worker ---------------------------------------------------------------------- */

static void rt_set_state(akeno_app *app, const char *state, const char *detail) {
    pthread_mutex_lock(&app->lock);
    app_copy_str(app->rt_state, sizeof(app->rt_state), state);
    app_copy_str(app->rt_detail, sizeof(app->rt_detail), detail);
    app->rt_changed_at = akeno_now_ms();
    pthread_mutex_unlock(&app->lock);
}

static void rt_on_state(void *user, const char *state, const char *detail) {
    akeno_app *app = user;
    rt_set_state(app, state, detail);
    akeno_buf data;
    akeno_buf_init(&data, 1024);
    int rc = akeno_buf_append_str(&data, "{\"state\":");
    rc |= akeno_buf_append_json_string(&data, state);
    rc |= akeno_buf_append_str(&data, ",\"detail\":");
    rc |= akeno_buf_append_json_string(&data, detail);
    rc |= akeno_buf_append_str(&data, "}");
    if (rc == 0) app_push_event(app, "realtime.state", data.data);
    akeno_buf_free(&data);
    app_log(app, AKENO_LOG_INFO, "realtime.state", "%s: %s", state, detail);
}

static void rt_on_event(void *user, const char *type, const akeno_json *data) {
    akeno_app *app = user;
    if (strcmp(type, "typing") == 0) return; /* Not shown in this version. */
    akeno_buf json;
    akeno_buf_init(&json, APP_EVENT_MAX_BYTES);
    int fits = akeno_json_write(data, &json) == 0;
    app_push_event(app, type, fits ? json.data : NULL);
    akeno_buf_free(&json);

    char self[40];
    pthread_mutex_lock(&app->lock);
    app_copy_str(self, sizeof(self), app->online_id);
    pthread_mutex_unlock(&app->lock);
    if (strcmp(type, "message.created") == 0) {
        on_message(app, self, data);
        return;
    }
    char text[400];
    int social = 1;
    if (notification_text(type, data, text, sizeof(text), &social)) app_notify(app, social, text);
}

static int rt_should_stop(void *user) {
    akeno_app *app = user;
    return app->stop || app->rt_stop || g_exit_requested;
}

static void *rt_main(void *arg) {
    akeno_app *app = arg;
    akeno_client *client = NULL;
    akeno_status status = app_make_client(app, &client);
    if (status == AKENO_OK && !akeno_client_is_paired(client)) status = AKENO_ERR_NOT_PAIRED;
    if (status == AKENO_OK) {
        akeno_realtime_options options = { 0 };
        options.on_event = rt_on_event;
        options.on_state = rt_on_state;
        options.should_stop = rt_should_stop;
        options.user = app;
        options.backoff_base_ms = app->opt.backoff_base_ms;
        options.max_consecutive_failures = app->opt.max_reconnects;
        status = akeno_realtime_run(client, &options);
    }
    const char *state = "offline";
    const char *detail = "Live updates stopped.";
    switch (status) {
    case AKENO_ERR_STOPPED: state = "stopped"; detail = "Live updates stopped."; break;
    case AKENO_ERR_UNAUTHORIZED: state = "signed-out"; detail = "This console was unpaired on the website."; break;
    case AKENO_ERR_FORBIDDEN: state = "not-allowed"; detail = "Live updates were not permitted when this console was paired."; break;
    case AKENO_ERR_GAVE_UP: state = "offline"; detail = "The server could not be reached. Use Reconnect to try again."; break;
    default: detail = akeno_status_name(status); break;
    }
    rt_set_state(app, state, detail);
    if (status == AKENO_ERR_UNAUTHORIZED) {
        akeno_forget_credentials(client);
        pthread_mutex_lock(&app->lock);
        app->paired = 0;
        app->online_id[0] = '\0';
        app->credentials_changed = 1;
        pthread_mutex_unlock(&app->lock);
        app_log(app, AKENO_LOG_WARN, "app.unpaired", "The server signed this console out; the credential was deleted.");
        app_notify(app, 1, "AKENO: This console was signed out. Open AKENO NETWORK to pair it again.");
    }
    akeno_client_destroy(client);
    pthread_mutex_lock(&app->lock);
    if (status != AKENO_ERR_STOPPED) app->rt_wanted = 0; /* Fatal or gave up: wait for Reconnect. */
    app->rt_thread_done = 1;
    pthread_mutex_unlock(&app->lock);
    return NULL;
}

/* ---- Pairing worker ----------------------------------------------------------------------- */

static int pair_should_stop(void *user) {
    akeno_app *app = user;
    return app->stop || app->pair_cancel || g_exit_requested;
}

static void *pair_main(void *arg) {
    akeno_app *app = arg;
    akeno_status status = akeno_pairing_wait(app->pair_client, &app->pairing, pair_should_stop, app);
    pthread_mutex_lock(&app->lock);
    switch (status) {
    case AKENO_OK: app->pair_state = PAIR_DONE; app->credentials_changed = 1; break;
    case AKENO_ERR_PAIRING_DENIED: app->pair_state = PAIR_DENIED; break;
    case AKENO_ERR_PAIRING_EXPIRED: app->pair_state = PAIR_EXPIRED; break;
    case AKENO_ERR_STOPPED: app->pair_state = PAIR_CANCELLED; break;
    default:
        app->pair_state = PAIR_FAILED;
        snprintf(app->pair_error, sizeof(app->pair_error), "Pairing failed (%s).", akeno_status_name(status));
        break;
    }
    char online_id[40];
    app_copy_str(online_id, sizeof(online_id), status == AKENO_OK ? akeno_client_online_id(app->pair_client) : "");
    memset(&app->pairing, 0, sizeof(app->pairing));
    app->pair_thread_done = 1;
    pthread_mutex_unlock(&app->lock);
    app_log(app, status == AKENO_OK ? AKENO_LOG_INFO : AKENO_LOG_WARN, "pairing.finished", "Pairing ended: %s.", akeno_status_name(status));
    if (status == AKENO_OK) {
        char text[120];
        snprintf(text, sizeof(text), "AKENO: This console is now paired as %s.", online_id);
        app_notify(app, 1, text);
    }
    return NULL;
}

akeno_status app_pair_start(akeno_app *app, char *err, size_t err_len) {
    err[0] = '\0';
    if (app->pair_thread_running) {
        snprintf(err, err_len, "Pairing is already in progress.");
        return AKENO_ERR_ARGUMENT;
    }
    akeno_client *client = NULL;
    akeno_status status = app_make_client(app, &client);
    if (status != AKENO_OK) {
        snprintf(err, err_len, "Set a valid https:// server address first.");
        return status;
    }
    char name[64];
    pthread_mutex_lock(&app->lock);
    app_copy_str(name, sizeof(name), app->device_name);
    pthread_mutex_unlock(&app->lock);
    akeno_pairing pairing;
    akeno_response response;
    status = akeno_pairing_start(client, name, AKENO_PLATFORM_LABEL, &pairing, &response);
    if (status != AKENO_OK) {
        if (response.error_message[0]) snprintf(err, err_len, "%s", response.error_message);
        else if (status == AKENO_ERR_NOT_FOUND) snprintf(err, err_len, "This server does not support device pairing. Update the AKENO server.");
        else if (status == AKENO_ERR_TLS) snprintf(err, err_len, "The server's certificate could not be verified. See Diagnostics.");
        else snprintf(err, err_len, "Could not reach the server (%s).", akeno_status_name(status));
        akeno_response_free(&response);
        akeno_client_destroy(client);
        pthread_mutex_lock(&app->lock);
        app->pair_state = PAIR_FAILED;
        app_copy_str(app->pair_error, sizeof(app->pair_error), err);
        pthread_mutex_unlock(&app->lock);
        return status;
    }
    akeno_response_free(&response);
    pthread_mutex_lock(&app->lock);
    app->pairing = pairing;
    app->pair_client = client;
    app->pair_state = PAIR_WAITING;
    app->pair_error[0] = '\0';
    app_copy_str(app->pair_code, sizeof(app->pair_code), pairing.user_code);
    app_copy_str(app->pair_uri, sizeof(app->pair_uri), pairing.verification_uri);
    app_copy_str(app->pair_uri_complete, sizeof(app->pair_uri_complete), pairing.verification_uri_complete);
    app->pair_expires_at = akeno_now_ms() + (long long)pairing.expires_in * 1000;
    app->pair_cancel = 0;
    app->pair_thread_done = 0;
    pthread_mutex_unlock(&app->lock);
    if (app_start_worker(&app->pair_thread, pair_main, app) != 0) {
        akeno_client_destroy(client);
        pthread_mutex_lock(&app->lock);
        app->pair_client = NULL;
        app->pair_state = PAIR_FAILED;
        pthread_mutex_unlock(&app->lock);
        snprintf(err, err_len, "Could not start the pairing worker.");
        return AKENO_ERR_NO_MEMORY;
    }
    app->pair_thread_running = 1;
    app_log(app, AKENO_LOG_INFO, "pairing.started", "Waiting for approval of code %s.", pairing.user_code);
    char text[400];
    snprintf(text, sizeof(text), "AKENO: Pairing code %s - enter it at %s", pairing.user_code, pairing.verification_uri);
    app_notify(app, 1, text);
    return AKENO_OK;
}

void app_pair_cancel(akeno_app *app) {
    app->pair_cancel = 1;
}

/* ---- Main-thread housekeeping ------------------------------------------------------------- */

static void start_realtime(akeno_app *app) {
    pthread_mutex_lock(&app->lock);
    app->rt_stop = 0;
    app->rt_thread_done = 0;
    pthread_mutex_unlock(&app->lock);
    if (app_start_worker(&app->rt_thread, rt_main, app) == 0) {
        app->rt_thread_running = 1;
    } else {
        app_log(app, AKENO_LOG_ERROR, "realtime.thread_failed", "Could not start the realtime worker.");
        pthread_mutex_lock(&app->lock);
        app->rt_wanted = 0;
        pthread_mutex_unlock(&app->lock);
    }
}

void app_tick(akeno_app *app) {
    pthread_mutex_lock(&app->lock);
    int reload = app->credentials_changed;
    app->credentials_changed = 0;
    int pair_done = app->pair_thread_running && app->pair_thread_done;
    int rt_done = app->rt_thread_running && app->rt_thread_done;
    pthread_mutex_unlock(&app->lock);

    if (pair_done) {
        pthread_join(app->pair_thread, NULL);
        app->pair_thread_running = 0;
        akeno_client_destroy(app->pair_client);
        app->pair_client = NULL;
    }
    if (rt_done) {
        pthread_join(app->rt_thread, NULL);
        app->rt_thread_running = 0;
    }
    if (reload) reload_ui_client(app);

    pthread_mutex_lock(&app->lock);
    int paired = app->paired;
    int wanted = app->rt_wanted;
    if (!paired && app->rt_thread_running) app->rt_stop = 1;
    pthread_mutex_unlock(&app->lock);
    if (paired && wanted && !app->rt_thread_running && !app->stop) start_realtime(app);
    app_activity_tick(app);
    app_quick_access_tick(app);
}

int app_open_ui(akeno_app *app) { return app_open_ui_at(app, ""); }

int app_open_ui_at(akeno_app *app, const char *open) {
    char url[260];
    int with_target = open && open[0] && app_valid_open(open);
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/#t=%s%s%s", app->port, app->session_token, with_target ? "&open=" : "",
             with_target ? open : "");
    int rc = akeno_platform_open_browser(url);
    memset(url, 0, sizeof(url));
    pthread_mutex_lock(&app->lock);
    app->browser_launches++;
    app->browser_last_rc = rc;
    pthread_mutex_unlock(&app->lock);
    if (rc == 0) {
        app_log(app, AKENO_LOG_INFO, "ui.browser_opened", "Asked the system to open the AKENO NETWORK page.");
    } else {
        app_log(app, AKENO_LOG_WARN, "ui.browser_failed", "The system browser could not be opened.");
        app_notify(app, 1, "AKENO: The browser could not be opened. AKENO keeps running and shows notifications.");
    }
    return rc;
}

/* Without a page, pairing still works: the code is delivered as a notification. */
static void pair_without_page(akeno_app *app) {
    pthread_mutex_lock(&app->lock);
    int ready = app->server[0] && !app->paired;
    pthread_mutex_unlock(&app->lock);
    if (!ready) return;
    char err[200];
    if (app_pair_start(app, err, sizeof(err)) == AKENO_OK) {
        app_log(app, AKENO_LOG_INFO, "pairing.without_page", "Started pairing; the code was sent as a notification.");
    } else {
        char text[260];
        snprintf(text, sizeof(text), "AKENO: Pairing could not start: %s", err);
        app_notify(app, 1, text);
    }
}

/* ---- Entry point -------------------------------------------------------------------------- */

static int ensure_dir(const char *path) {
    if (mkdir(path, 0700) == 0 || errno == EEXIST) {
        struct stat st;
        return stat(path, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return -1;
}

static void shutdown_workers(akeno_app *app) {
    app->stop = 1;
    app->pair_cancel = 1;
    app->rt_stop = 1;
    app_quick_access_shutdown(app);
    app_activity_shutdown(app); /* Reports the end of a running game session first (bounded by the I/O timeout). */
    if (app->pair_thread_running) {
        pthread_join(app->pair_thread, NULL);
        app->pair_thread_running = 0;
        akeno_client_destroy(app->pair_client);
        app->pair_client = NULL;
    }
    if (app->rt_thread_running) {
        pthread_join(app->rt_thread, NULL);
        app->rt_thread_running = 0;
    }
}

int akeno_app_main(const akeno_app_options *options) {
    if (!options || !options->data_dir || strlen(options->data_dir) > 400) return 2;
    akeno_app *app = calloc(1, sizeof(*app));
    if (!app) return 1;
    app->opt = *options;
    pthread_mutex_init(&app->lock, NULL);
    pthread_mutex_init(&app->log_lock, NULL);
    app->started_at = akeno_now_ms();
    app->notify_tokens = NOTIFY_BURST;
    app->notify_refill_at = app->started_at;
    app_copy_str(app->rt_state, sizeof(app->rt_state), "idle");
    app_copy_str(app->qa.state, sizeof(app->qa.state), "off");
    app->qa.config_gen = 1; /* != failed_gen (0): a shortcut turned on in the settings starts at launch */
    app->qa.pad.user_id = -1;
    app->qa.pad.handle = -1;

    app_copy_str(app->data_dir, sizeof(app->data_dir), options->data_dir);
    snprintf(app->settings_path, sizeof(app->settings_path), "%s/settings.json", app->data_dir);
    snprintf(app->credential_path, sizeof(app->credential_path), "%s/device.json", app->data_dir);
    snprintf(app->log_path, sizeof(app->log_path), "%s/akeno.log", app->data_dir);
    snprintf(app->instance_path, sizeof(app->instance_path), "%s/instance.key", app->data_dir);
#ifdef AKENO_NATIVE_IPC
    snprintf(app->native_path, sizeof(app->native_path), "%s/native.key", app->data_dir);
#endif
    snprintf(app->ca_path, sizeof(app->ca_path), "%s/ca.pem", app->data_dir);

    akeno_platform_init(&app->platform);
    int exit_code = 0;
    int listen_fd = -1;
    if (ensure_dir(app->data_dir) != 0) {
        akeno_platform_notify("AKENO: Could not create the data folder. Nothing was changed.");
        exit_code = 1;
        goto out;
    }
    open_log(app);
    app_log(app, AKENO_LOG_INFO, "app.start", "AKENO NETWORK console app %s (%s) on %s %s.", AKENO_APP_VERSION, AKENO_BUILD_ID,
            app->platform.name, app->platform.firmware[0] ? app->platform.firmware : "");

    if (akeno_global_init() != AKENO_OK) {
        app_log(app, AKENO_LOG_ERROR, "app.tls_init_failed", "TLS could not be initialized (no secure random source?).");
        akeno_platform_notify("AKENO: Secure connections are unavailable on this system. AKENO stopped.");
        exit_code = 1;
        goto out;
    }

    struct stat st;
    if (options->ca_file) {
        app->ca_file = options->ca_file;
        app->custom_ca = 1;
    } else if (stat(app->ca_path, &st) == 0 && S_ISREG(st.st_mode)) {
        app->ca_file = app->ca_path;
        app->custom_ca = 1;
    }

    load_settings(app);
    app_activity_init(app);
#ifdef AKENO_EXPERIMENTAL_AUDIO
    char report_path[620];
    snprintf(report_path, sizeof(report_path), "%s/audiolab-report.txt", app->data_dir);
    audio_lab_init(report_path, AKENO_BUILD_ID, app->platform.firmware);
#endif
    if (options->server && options->server[0]) {
        app_copy_str(app->server, sizeof(app->server), options->server);
        app_save_settings(app);
    }
    reload_ui_client(app);

    int in_use = 0;
    listen_fd = ui_server_listen(options->ui_port, &app->port, &in_use);
    if (listen_fd < 0) {
        /* A second launch while AKENO is running brings the running instance's page back. */
        char key[80] = "";
        int fd = open(app->instance_path, O_RDONLY);
        if (fd >= 0) {
            ssize_t n = read(fd, key, sizeof(key) - 1);
            close(fd);
            key[n > 0 ? n : 0] = '\0';
        }
        if (in_use && key[0] && ui_server_signal_existing(options->ui_port, key) == 0) {
            app_log(app, AKENO_LOG_INFO, "app.already_running", "AKENO is already running; asked it to show its page.");
            exit_code = 0;
        } else {
            app_log(app, AKENO_LOG_ERROR, "ui.listen_failed", "Port %d on 127.0.0.1 is not available.", options->ui_port);
            char text[120];
            snprintf(text, sizeof(text), "AKENO: Port %d is used by another program. AKENO stopped.", options->ui_port);
            akeno_platform_notify(text);
            exit_code = 1;
        }
        goto out_tls;
    }
    random_hex(app, app->session_token, 32);
    random_hex(app, app->instance_key, 32);
    if (write_private_file(app->instance_path, app->instance_key, strlen(app->instance_key)) != 0)
        app_log(app, AKENO_LOG_WARN, "app.instance_key", "Could not write the instance key; a second launch cannot reopen the page.");
#ifdef AKENO_NATIVE_IPC
    random_hex(app, app->native_key, 32);
    if (write_private_file(app->native_path, app->native_key, strlen(app->native_key)) != 0)
        app_log(app, AKENO_LOG_WARN, "native.key", "Could not write the native key; AKENO Native cannot connect.");
    else
        app_log(app, AKENO_LOG_INFO, "native.ready", "Native interface %d ready for AKENO Native (key file readable by the owner only).",
                AKENO_NATIVE_IPC_VERSION);
#endif
    app_log(app, AKENO_LOG_INFO, "ui.listening", "Local page on http://127.0.0.1:%d/ (this console only).", app->port);

    akeno_platform_notify("AKENO NETWORK " AKENO_APP_RELEASE " started.");
    if (options->open_browser) {
        if (app_open_ui(app) != 0) pair_without_page(app);
    } else {
        char url[200];
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/#t=%s", app->port, app->session_token);
        akeno_platform_open_browser(url); /* Host build: prints the URL. */
    }

    ui_server_loop(app, listen_fd);

    app_log(app, AKENO_LOG_INFO, "app.stopping", "Stopping.");
#ifdef AKENO_EXPERIMENTAL_AUDIO
    audio_lab_shutdown();
#endif
    shutdown_workers(app);
    close(listen_fd);
    unlink(app->instance_path);
#ifdef AKENO_NATIVE_IPC
    unlink(app->native_path);
#endif
    akeno_platform_notify("AKENO NETWORK stopped.");

out_tls:
    shutdown_workers(app);
    akeno_client_destroy(app->ui_client);
    akeno_global_cleanup();
out:
    app_log(app, AKENO_LOG_INFO, "app.exit", "Exit code %d.", exit_code);
    akeno_platform_cleanup();
    for (int i = 0; i < APP_EVENT_RING; i++) free(app->events[i].json);
    if (app->log) fclose(app->log);
    memset(app->session_token, 0, sizeof(app->session_token));
    memset(app->instance_key, 0, sizeof(app->instance_key));
#ifdef AKENO_NATIVE_IPC
    memset(app->native_key, 0, sizeof(app->native_key));
#endif
    pthread_mutex_destroy(&app->lock);
    pthread_mutex_destroy(&app->log_lock);
    free(app);
    return exit_code;
}

int app_exit_requested(void) { return g_exit_requested; }
