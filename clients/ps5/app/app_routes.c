/*
 * Local API used by the console UI (served on 127.0.0.1 only). Every /api/ route requires the
 * per-launch session secret in X-Akeno-Token and a loopback Host header; see ui_server.c.
 * The device token never leaves this process: the UI talks to the AKENO server only through
 * the allow-listed proxy below.
 */
#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "akeno/caps.h"
#include "app_internal.h"
#ifdef AKENO_EXPERIMENTAL_AUDIO
#include "audio_lab.h"
#endif
#include "crypto_util.h"
#include "json_write.h"
#include "transport.h"
#include "ui_http.h"
#include "url.h"

int app_exit_requested(void);

/* ---- Response helpers --------------------------------------------------------------------- */

static void respond_json(ui_response *r, int status) {
    r->status = status;
    r->content_type = "application/json; charset=utf-8";
}

static void respond_error(ui_response *r, int status, const char *code, const char *message) {
    respond_json(r, status);
    akeno_buf_reset(&r->body);
    akeno_buf_append_str(&r->body, "{\"ok\":false,\"error\":{\"code\":");
    akeno_buf_append_json_string(&r->body, code);
    akeno_buf_append_str(&r->body, ",\"message\":");
    akeno_buf_append_json_string(&r->body, message);
    akeno_buf_append_str(&r->body, "}}");
}

static void respond_ok(ui_response *r) {
    respond_json(r, 200);
    akeno_buf_reset(&r->body);
    akeno_buf_append_str(&r->body, "{\"ok\":true}");
}

static void json_kv_str(akeno_buf *b, const char *key, const char *value, int comma) {
    if (comma) akeno_buf_append_str(b, ",");
    akeno_buf_append_json_string(b, key);
    akeno_buf_append_str(b, ":");
    akeno_buf_append_json_string(b, value ? value : "");
}

static void json_kv_int(akeno_buf *b, const char *key, long long value, int comma) {
    if (comma) akeno_buf_append_str(b, ",");
    akeno_buf_append_json_string(b, key);
    akeno_buf_printf(b, ":%lld", value);
}

static void json_kv_bool(akeno_buf *b, const char *key, int value, int comma) {
    if (comma) akeno_buf_append_str(b, ",");
    akeno_buf_append_json_string(b, key);
    akeno_buf_append_str(b, value ? ":true" : ":false");
}

static const char *pair_state_name(app_pair_state s) {
    switch (s) {
    case PAIR_IDLE: return "idle";
    case PAIR_WAITING: return "waiting";
    case PAIR_DONE: return "paired";
    case PAIR_DENIED: return "denied";
    case PAIR_EXPIRED: return "expired";
    case PAIR_FAILED: return "failed";
    case PAIR_CANCELLED: return "cancelled";
    }
    return "idle";
}

/* ---- /api/state --------------------------------------------------------------------------- */

static void inbox_summary_json(const akeno_app *app, akeno_buf *b);

static void route_state(akeno_app *app, ui_response *r) {
    akeno_buf *b = &r->body;
    long long now = akeno_now_ms();
    pthread_mutex_lock(&app->lock);
    akeno_buf_append_str(b, "{\"ok\":true,\"app\":{");
    json_kv_str(b, "version", AKENO_APP_VERSION, 0);
    json_kv_str(b, "release", AKENO_APP_RELEASE, 1);
    json_kv_str(b, "build", AKENO_BUILD_ID, 1);
    json_kv_str(b, "platform", app->platform.name, 1);
    json_kv_str(b, "firmware", app->platform.firmware, 1);
#ifdef AKENO_EXPERIMENTAL_AUDIO
    json_kv_bool(b, "audioLab", 1, 1);
#endif
    akeno_buf_append_str(b, "}");
    json_kv_bool(b, "configured", app->server[0] != '\0', 1);
    json_kv_str(b, "server", app->server, 1);
    json_kv_str(b, "deviceName", app->device_name, 1);
    json_kv_bool(b, "paired", app->paired, 1);
    json_kv_str(b, "onlineId", app->online_id, 1);
    akeno_buf_append_str(b, ",\"pairing\":{");
    json_kv_str(b, "state", pair_state_name(app->pair_state), 0);
    if (app->pair_state == PAIR_WAITING) {
        json_kv_str(b, "userCode", app->pair_code, 1);
        json_kv_str(b, "verificationUri", app->pair_uri, 1);
        json_kv_str(b, "verificationUriComplete", app->pair_uri_complete, 1);
        long long left = (app->pair_expires_at - now) / 1000;
        json_kv_int(b, "expiresIn", left > 0 ? left : 0, 1);
    }
    json_kv_str(b, "error", app->pair_error, 1);
    akeno_buf_append_str(b, "},\"realtime\":{");
    json_kv_str(b, "state", app->rt_state, 0);
    json_kv_str(b, "detail", app->rt_detail, 1);
    json_kv_int(b, "secondsInState", app->rt_changed_at ? (now - app->rt_changed_at) / 1000 : 0, 1);
    akeno_buf_append_str(b, "},\"notifications\":{");
    json_kv_bool(b, "messages", app->notify_messages, 0);
    json_kv_bool(b, "social", app->notify_social, 1);
    json_kv_bool(b, "actionable", app->actionable_notifications, 1);
    akeno_buf_append_str(b, "},\"activity\":");
    app_activity_json(app, b, 0);
    akeno_buf_append_str(b, ",\"inbox\":");
    inbox_summary_json(app, b);
    akeno_buf_append_str(b, ",\"quickAccess\":");
    app_quick_access_json(app, b);
    json_kv_int(b, "eventsNext", (long long)app->next_seq, 1);
    akeno_buf_append_str(b, "}");
    pthread_mutex_unlock(&app->lock);
    respond_json(r, 200);
}

/* ---- /api/inbox: recent messages for quick access ----------------------------------------- */

/* Caller holds app->lock. */
static void recent_json(const app_recent_message *m, long long now, akeno_buf *b) {
    akeno_buf_printf(b, "{\"seq\":%llu", m->seq);
    json_kv_str(b, "open", m->open, 1);
    json_kv_str(b, "kind", m->kind, 1);
    json_kv_str(b, "from", m->from, 1);
    json_kv_str(b, "preview", m->preview, 1);
    json_kv_int(b, "secondsAgo", (now - m->at) / 1000, 1);
    akeno_buf_append_str(b, "}");
}

/* {"latestSeq":N,"seenSeq":N,"unseen":N,"latest":{...}|null}; caller holds app->lock. */
static void inbox_summary_json(const akeno_app *app, akeno_buf *b) {
    long long now = akeno_now_ms();
    int unseen = 0;
    for (int i = 0; i < APP_RECENT_MESSAGES; i++)
        if (app->recent[i].seq && app->recent[i].seq > app->recent_seen_seq) unseen++;
    akeno_buf_printf(b, "{\"latestSeq\":%llu,\"seenSeq\":%llu,\"unseen\":%d,\"latest\":", app->recent_seq, app->recent_seen_seq, unseen);
    const app_recent_message *m = app->recent_seq ? &app->recent[app->recent_seq % APP_RECENT_MESSAGES] : NULL;
    if (m && m->seq == app->recent_seq) recent_json(m, now, b);
    else akeno_buf_append_str(b, "null");
    akeno_buf_append_str(b, "}");
}

static void route_inbox(akeno_app *app, ui_response *r) {
    akeno_buf *b = &r->body;
    long long now = akeno_now_ms();
    pthread_mutex_lock(&app->lock);
    akeno_buf_printf(b, "{\"ok\":true,\"seenSeq\":%llu,\"messages\":[", app->recent_seen_seq);
    int first = 1;
    unsigned long long newest = app->recent_seq;
    for (unsigned long long seq = newest; seq > 0 && seq + APP_RECENT_MESSAGES > newest; seq--) {
        const app_recent_message *m = &app->recent[seq % APP_RECENT_MESSAGES];
        if (m->seq != seq) continue;
        if (!first) akeno_buf_append_str(b, ",");
        recent_json(m, now, b);
        first = 0;
    }
    akeno_buf_append_str(b, "]}");
    pthread_mutex_unlock(&app->lock);
    respond_json(r, 200);
}

static void route_inbox_seen(akeno_app *app, const akeno_json *root, ui_response *r) {
    int64_t seq = 0;
    if (!root || akeno_json_int64(akeno_json_get(root, "seq"), &seq) != 0 || seq < 0) {
        respond_error(r, 400, "INVALID_BODY", "Expected {\"seq\": number}.");
        return;
    }
    pthread_mutex_lock(&app->lock);
    if ((unsigned long long)seq > app->recent_seq) seq = (int64_t)app->recent_seq;
    if ((unsigned long long)seq > app->recent_seen_seq) app->recent_seen_seq = (unsigned long long)seq;
    pthread_mutex_unlock(&app->lock);
    respond_ok(r);
}

/* ---- /api/settings ------------------------------------------------------------------------ */

static int valid_server_url(const char *text) {
    akeno_url url;
    if (strlen(text) >= 300 || akeno_url_parse(text, &url) != 0) return 0;
    return url.tls || akeno_url_is_loopback(&url);
}

static void route_settings(akeno_app *app, const akeno_json *root, ui_response *r) {
    if (!root || root->type != AKENO_JSON_OBJECT) {
        respond_error(r, 400, "INVALID_BODY", "Expected a JSON object.");
        return;
    }
    const char *server = akeno_json_get_string(root, "server", NULL);
    const char *name = akeno_json_get_string(root, "deviceName", NULL);
    pthread_mutex_lock(&app->lock);
    int paired = app->paired;
    int pairing = app->pair_state == PAIR_WAITING;
    int server_changed = server && strcmp(server, app->server) != 0;
    pthread_mutex_unlock(&app->lock);
    if (server_changed) {
        if (paired || pairing) {
            respond_error(r, 409, "UNPAIR_FIRST", "Unpair this console before changing the server.");
            return;
        }
        if (!valid_server_url(server)) {
            respond_error(r, 400, "INVALID_SERVER", "Enter the server address as https://host (for example https://akeno.example.org).");
            return;
        }
    }
    if (name && (strlen(name) == 0 || strlen(name) > 48)) {
        respond_error(r, 400, "INVALID_NAME", "The console name must be 1 to 48 characters long.");
        return;
    }
    const char *combo = akeno_json_get_string(root, "quickAccessCombo", NULL);
    if (combo && !akeno_shortcut_combo_find(combo)) {
        respond_error(r, 400, "INVALID_SHORTCUT", "Choose one of the offered controller combinations.");
        return;
    }
    int64_t hold = 0;
    const akeno_json *hold_json = akeno_json_get(root, "quickAccessHoldMs");
    if (hold_json && (akeno_json_int64(hold_json, &hold) != 0 || !akeno_shortcut_hold_valid((int)hold))) {
        respond_error(r, 400, "INVALID_HOLD", "Choose a hold time of 1, 1.5 or 2 seconds.");
        return;
    }
    pthread_mutex_lock(&app->lock);
    if (server_changed) snprintf(app->server, sizeof(app->server), "%s", server);
    if (name) snprintf(app->device_name, sizeof(app->device_name), "%s", name);
    app->notify_messages = akeno_json_get_bool(root, "notifyMessages", app->notify_messages);
    app->notify_social = akeno_json_get_bool(root, "notifySocial", app->notify_social);
    int activity_before = app->activity_enabled;
    app->activity_enabled = akeno_json_get_bool(root, "activityEnabled", app->activity_enabled);
    if (app->activity_enabled && !activity_before) app->act_wanted = 1;
    app->actionable_notifications = akeno_json_get_bool(root, "actionableNotifications", app->actionable_notifications);
    int qa_before = app->qa_shortcut;
    app->qa_shortcut = akeno_json_get_bool(root, "quickAccessShortcut", app->qa_shortcut);
    int qa_changed = qa_before != app->qa_shortcut || combo || hold_json;
    if (combo) app_copy_str(app->qa_combo, sizeof(app->qa_combo), combo);
    if (hold_json) app->qa_hold_ms = (int)hold;
    if (qa_changed) app->qa.config_gen++;
    if (server_changed) app->credentials_changed = 1;
    pthread_mutex_unlock(&app->lock);
    if (app_save_settings(app) != AKENO_OK) {
        respond_error(r, 500, "STORAGE", "The settings could not be saved.");
        return;
    }
    if (server_changed) app_log(app, AKENO_LOG_INFO, "settings.server", "Server set to %s.", server);
    if (qa_changed)
        app_log(app, AKENO_LOG_INFO, "settings.quickaccess", "Controller shortcut %s (%s, hold %d ms).", app->qa_shortcut ? "on" : "off",
                app->qa_combo, app->qa_hold_ms);
    app_tick(app);
    respond_ok(r);
}

/* ---- Pairing and sign-out ----------------------------------------------------------------- */

static void route_pair_start(akeno_app *app, ui_response *r) {
    pthread_mutex_lock(&app->lock);
    int paired = app->paired;
    pthread_mutex_unlock(&app->lock);
    if (paired) {
        respond_error(r, 409, "ALREADY_PAIRED", "This console is already paired.");
        return;
    }
    char err[200];
    akeno_status status = app_pair_start(app, err, sizeof(err));
    if (status != AKENO_OK) {
        respond_error(r, 502, akeno_status_name(status), err);
        return;
    }
    respond_ok(r);
}

static void route_logout(akeno_app *app, ui_response *r) {
    if (app->ui_client && akeno_client_is_paired(app->ui_client)) {
        akeno_logout(app->ui_client); /* Revokes on the server (best effort) and deletes the credential. */
    }
    app_mark_unpaired(app, "Unpaired from the console.");
    pthread_mutex_lock(&app->lock);
    app->pair_state = PAIR_IDLE;
    app->pair_error[0] = '\0';
    pthread_mutex_unlock(&app->lock);
    app_tick(app);
    respond_ok(r);
}

/* ---- Images -------------------------------------------------------------------------------- */

/*
 * The page shows images as data: URLs (its CSP allows only 'self' and data: images, and every
 * /api/ request needs the session secret header, which an <img> cannot send). Images are small:
 * server variants are capped at 512 KiB and console icons at AKENO_ICON_MAX_BYTES.
 */
#define MEDIA_MAX_BYTES (512 * 1024)

static void respond_image(ui_response *r, const char *type, const unsigned char *data, size_t len, const char *source) {
    size_t need = (len + 2) / 3 * 4 + 1;
    char *encoded = malloc(need);
    if (!encoded || akeno_base64_encode(data, len, encoded, need) != 0) {
        free(encoded);
        respond_error(r, 500, "NO_MEMORY", "Out of memory.");
        return;
    }
    respond_json(r, 200);
    akeno_buf_reset(&r->body);
    akeno_buf_append_str(&r->body, "{\"ok\":true");
    json_kv_str(&r->body, "type", type, 1);
    if (source) json_kv_str(&r->body, "source", source, 1);
    akeno_buf_append_str(&r->body, ",\"data\":\"");
    if (akeno_buf_append_str(&r->body, encoded) != 0 || akeno_buf_append_str(&r->body, "\"}") != 0) {
        respond_error(r, 500, "TOO_LARGE", "The image is too large.");
    }
    free(encoded);
}

/* GET /api/media?id=<media id>&v=sm|md|lg|fallback: a profile picture, banner or game artwork. */
static void route_media(akeno_app *app, const ui_request *req, ui_response *r) {
    char id[40] = "", variant[16] = "";
    ui_http_query_param(req->query, "id", id, sizeof(id));
    ui_http_query_param(req->query, "v", variant, sizeof(variant));
    const char *file = strcmp(variant, "sm") == 0   ? "sm.webp"
                       : strcmp(variant, "md") == 0 ? "md.webp"
                       : strcmp(variant, "lg") == 0 ? "lg.webp"
                                                    : "fallback.jpg";
    if (!app->ui_client || !akeno_client_is_paired(app->ui_client)) {
        respond_error(r, 401, "NOT_PAIRED", "This console is not paired.");
        return;
    }
    unsigned char *data = NULL;
    size_t len = 0;
    char type[32];
    akeno_status status = akeno_fetch_media(app->ui_client, id, file, MEDIA_MAX_BYTES, &data, &len, type, sizeof(type));
    if (status != AKENO_OK) {
        free(data);
        if (status == AKENO_ERR_UNAUTHORIZED) {
            app_mark_unpaired(app, "The server no longer accepts this console's credential.");
            app_tick(app);
        }
        respond_error(r, status == AKENO_ERR_ARGUMENT ? 400 : status == AKENO_ERR_NOT_FOUND ? 404 : 502, akeno_status_name(status),
                      "The image could not be loaded.");
        return;
    }
    respond_image(r, type, data, len, NULL);
    free(data);
}

/* GET /api/local-art?title=PPSA01234: the icon of a title installed on this console (read-only). */
static void route_local_art(akeno_app *app, const ui_request *req, ui_response *r) {
    char title[AKENO_TITLE_ID_MAX] = "";
    ui_http_query_param(req->query, "title", title, sizeof(title));
    const char *root = akeno_platform_metadata_root();
    char source[160] = "";
    size_t len = 0;
    unsigned char *data = akeno_is_game_title_id(title) ? akeno_game_icon_lookup(root, title, &len, source, sizeof(source)) : NULL;
    (void)app;
    if (!data) {
        respond_error(r, 404, "NOT_FOUND", "No icon for this title on this console.");
        return;
    }
    respond_image(r, "image/png", data, len, source);
    free(data);
}

/* ---- API proxy ---------------------------------------------------------------------------- */

/* Device-API paths the UI may use. The server enforces the device's scopes in addition. */
static int proxy_allowed(const char *method, const char *api_path) {
    static const char *const prefixes[] = { "/users/", "/presence", "/friends", "/conversations", "/groups",
                                            "/parties", "/game-sessions", "/invitations", "/notifications" };
    if (strcmp(api_path, "/devices/current") == 0) return strcmp(method, "GET") == 0;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        size_t n = strlen(prefixes[i]);
        if (strncmp(api_path, prefixes[i], n) == 0 && (prefixes[i][n - 1] == '/' || api_path[n] == '\0' || api_path[n] == '/' || api_path[n] == '?'))
            return 1;
    }
    return 0;
}

static void route_proxy(akeno_app *app, const ui_request *req, const char *body, size_t body_len, ui_response *r) {
    char api_path[600];
    snprintf(api_path, sizeof(api_path), "%s%s%s", req->path + 7, req->query[0] ? "?" : "", req->query);
    if (strstr(api_path, "..") || !proxy_allowed(req->method, api_path)) {
        respond_error(r, 403, "NOT_ALLOWED", "The console app does not use this API.");
        return;
    }
    if (!app->ui_client || !akeno_client_is_paired(app->ui_client)) {
        respond_error(r, 401, "NOT_PAIRED", "This console is not paired.");
        return;
    }
    char *json_body = NULL;
    if (body_len > 0) {
        akeno_json_doc *doc = akeno_json_parse(body, body_len, NULL, 0);
        if (!doc) {
            respond_error(r, 400, "INVALID_BODY", "The request body is not valid JSON.");
            return;
        }
        akeno_json_free(doc);
        json_body = malloc(body_len + 1);
        if (!json_body) {
            respond_error(r, 500, "NO_MEMORY", "Out of memory.");
            return;
        }
        memcpy(json_body, body, body_len);
        json_body[body_len] = '\0';
    }
    akeno_response response;
    akeno_status status = akeno_request(app->ui_client, req->method, api_path, json_body, &response);
    free(json_body);
    if (status == AKENO_ERR_UNAUTHORIZED) {
        akeno_response_free(&response);
        app_mark_unpaired(app, "The server no longer accepts this console's credential.");
        app_tick(app);
        respond_error(r, 401, "UNPAIRED", "This console was unpaired. Pair it again.");
        return;
    }
    if (response.http_status == 0) {
        akeno_response_free(&response);
        respond_error(r, 502, akeno_status_name(status), "The AKENO server could not be reached.");
        return;
    }
    respond_json(r, 200);
    akeno_buf *b = &r->body;
    akeno_buf_reset(b);
    akeno_buf_printf(b, "{\"ok\":%s,\"status\":%d", status == AKENO_OK ? "true" : "false", response.http_status);
    if (status == AKENO_OK) {
        akeno_buf_append_str(b, ",\"data\":");
        if (akeno_json_write(akeno_response_json(&response), b) != 0) {
            akeno_response_free(&response);
            respond_error(r, 502, "TOO_LARGE", "The response was too large for the console app.");
            return;
        }
    } else {
        akeno_buf_append_str(b, ",\"error\":{");
        json_kv_str(b, "code", response.error_code[0] ? response.error_code : akeno_status_name(status), 0);
        json_kv_str(b, "message", response.error_message, 1);
        akeno_buf_append_str(b, "}");
    }
    akeno_buf_append_str(b, "}");
    akeno_response_free(&response);
}

/* ---- /api/events -------------------------------------------------------------------------- */

static void route_events(akeno_app *app, const ui_request *req, ui_response *r) {
    char after_text[24] = "0";
    ui_http_query_param(req->query, "after", after_text, sizeof(after_text));
    unsigned long long after = strtoull(after_text, NULL, 10);
    akeno_buf *b = &r->body;
    pthread_mutex_lock(&app->lock);
    unsigned long long next = app->next_seq;
    unsigned long long oldest = next >= APP_EVENT_RING ? next - APP_EVENT_RING + 1 : 1;
    int gap = after + 1 < oldest && after < next;
    akeno_buf_printf(b, "{\"ok\":true,\"next\":%llu,\"gap\":%s,\"events\":[", next, gap ? "true" : "false");
    int first = 1;
    for (unsigned long long seq = after + 1 > oldest ? after + 1 : oldest; seq <= next; seq++) {
        const app_event *e = &app->events[seq % APP_EVENT_RING];
        if (e->seq != seq || !e->json) continue;
        if (!first) akeno_buf_append_str(b, ",");
        if (akeno_buf_append_str(b, e->json) != 0) break;
        first = 0;
    }
    akeno_buf_append_str(b, "]}");
    pthread_mutex_unlock(&app->lock);
    respond_json(r, 200);
}

/* ---- Diagnostics -------------------------------------------------------------------------- */

static void route_diagnostics(akeno_app *app, ui_response *r) {
    akeno_buf *b = &r->body;
    akeno_transport_info transport;
    akeno_transport_get_info(&transport);
    struct stat st;
    int has_credential = stat(app->credential_path, &st) == 0;
    unsigned mode = has_credential ? (unsigned)(st.st_mode & 0777) : 0;

    akeno_platform_facts facts = { 0 };
    facts.platform = strcmp(app->platform.name, "PlayStation 5") == 0 ? AKENO_PLATFORM_PS5 : AKENO_PLATFORM_HOST;
    facts.platform_name = app->platform.name;
    facts.firmware = app->platform.firmware[0] ? app->platform.firmware : NULL;
    facts.has_sockets = 1;
    facts.has_tls = 1;
    facts.has_storage = 1;
    facts.has_notifications = facts.platform == AKENO_PLATFORM_PS5;
    akeno_capability_report caps;
    akeno_caps_evaluate(&facts, &caps);

    pthread_mutex_lock(&app->lock);
    akeno_buf_append_str(b, "{\"ok\":true");
    json_kv_str(b, "version", AKENO_APP_VERSION, 1);
    json_kv_str(b, "build", AKENO_BUILD_ID, 1);
    json_kv_str(b, "platform", app->platform.name, 1);
    json_kv_str(b, "firmware", app->platform.firmware, 1);
    json_kv_str(b, "firmwareNote", caps.firmware.note, 1);
    json_kv_int(b, "uptimeSeconds", (akeno_now_ms() - app->started_at) / 1000, 1);
    json_kv_str(b, "server", app->server, 1);
    json_kv_str(b, "dataDir", app->data_dir, 1);
    akeno_buf_append_str(b, ",\"credential\":{");
    json_kv_bool(b, "present", has_credential, 0);
    char mode_text[8];
    snprintf(mode_text, sizeof(mode_text), "%03o", mode);
    json_kv_str(b, "mode", has_credential ? mode_text : "", 1);
    akeno_buf_append_str(b, "},\"tls\":{");
    json_kv_str(b, "backend", transport.backend, 0);
    json_kv_str(b, "trustStore", app->custom_ca ? app->ca_file : transport.trust_store, 1);
    json_kv_bool(b, "customCa", app->custom_ca, 1);
    json_kv_int(b, "trustedRoots", transport.trusted_roots, 1);
    json_kv_int(b, "skippedRoots", transport.skipped_roots, 1);
    json_kv_str(b, "entropy", transport.entropy, 1);
    json_kv_str(b, "verification", "certificate chain and host name always verified", 1);
    akeno_buf_append_str(b, "},\"ui\":{");
    json_kv_int(b, "port", app->port, 0);
    json_kv_str(b, "browserMethod", app->platform.browser, 1);
    json_kv_int(b, "browserLaunches", app->browser_launches, 1);
    json_kv_int(b, "browserLastResult", app->browser_last_rc, 1);
    akeno_buf_append_str(b, "},\"notifications\":{");
    json_kv_str(b, "method", app->platform.notifications, 0);
    json_kv_int(b, "sent", app->notifications_sent, 1);
    json_kv_int(b, "failed", app->notifications_failed, 1);
    json_kv_int(b, "dropped", app->notifications_dropped, 1);
    akeno_buf_append_str(b, ",\"actionable\":{");
    json_kv_bool(b, "enabled", app->actionable_notifications, 0);
    json_kv_str(b, "method", app->platform.actionable, 1);
    json_kv_str(b, "status", akeno_platform_actionable_status(), 1);
    json_kv_int(b, "sent", app->actionable_sent, 1);
    json_kv_int(b, "refused", app->actionable_failed, 1);
    json_kv_int(b, "unavailable", app->actionable_unavailable, 1);
    akeno_buf_append_str(b, "}},\"activity\":");
    app_activity_json(app, b, 1);
    akeno_buf_append_str(b, ",\"realtime\":{");
    json_kv_str(b, "state", app->rt_state, 0);
    json_kv_str(b, "detail", app->rt_detail, 1);
    akeno_buf_append_str(b, "},\"quickAccess\":");
    app_quick_access_json(app, b);
    pthread_mutex_unlock(&app->lock);
#ifdef AKENO_EXPERIMENTAL_AUDIO
    json_kv_bool(b, "experimentalAudio", 1, 1); /* audio lab build: the page shows its section */
#endif

    akeno_buf_append_str(b, ",\"capabilities\":[");
    for (int f = 0; f < AKENO_FEATURE_COUNT; f++) {
        if (f) akeno_buf_append_str(b, ",");
        akeno_buf_append_str(b, "{");
        json_kv_str(b, "feature", akeno_feature_name((akeno_feature)f), 0);
        json_kv_str(b, "state", akeno_cap_state_name(caps.features[f].state), 1);
        json_kv_str(b, "reason", caps.features[f].reason, 1);
        akeno_buf_append_str(b, "}");
    }
    akeno_buf_append_str(b, "],\"log\":[");
    pthread_mutex_lock(&app->log_lock);
    for (int i = 0; i < app->log_count; i++) {
        int idx = (app->log_next - app->log_count + i + APP_LOG_RING) % APP_LOG_RING;
        if (i) akeno_buf_append_str(b, ",");
        akeno_buf_append_json_string(b, app->log_ring[idx]);
    }
    pthread_mutex_unlock(&app->log_lock);
    akeno_buf_append_str(b, "]}");
    respond_json(r, 200);
}

/* Opens a connection to the configured server and reports what was negotiated. */
static void route_connection_test(akeno_app *app, ui_response *r) {
    char server[300];
    pthread_mutex_lock(&app->lock);
    snprintf(server, sizeof(server), "%s", app->server);
    pthread_mutex_unlock(&app->lock);
    akeno_url url;
    if (!server[0] || akeno_url_parse(server, &url) != 0) {
        respond_error(r, 400, "NOT_CONFIGURED", "No server address is set.");
        return;
    }
    char err[256] = "";
    char tls[128] = "";
    akeno_conn *conn = NULL;
    long long started = akeno_now_ms();
    akeno_status status = akeno_conn_open(&url, app->ca_file, app->opt.io_timeout_ms > 0 ? app->opt.io_timeout_ms : 10000, &conn, err,
                                          sizeof(err));
    long long elapsed = akeno_now_ms() - started;
    if (conn) akeno_conn_describe(conn, tls, sizeof(tls));
    akeno_conn_close(conn);
    akeno_buf *b = &r->body;
    akeno_buf_append_str(b, "{\"ok\":");
    akeno_buf_append_str(b, status == AKENO_OK ? "true" : "false");
    json_kv_str(b, "result", akeno_status_name(status), 1);
    json_kv_str(b, "detail", status == AKENO_OK ? "Connected; the certificate and host name were verified." : err, 1);
    json_kv_str(b, "tls", tls, 1);
    json_kv_int(b, "milliseconds", elapsed, 1);
    akeno_buf_append_str(b, "}");
    respond_json(r, 200);
    app_log(app, status == AKENO_OK ? AKENO_LOG_INFO : AKENO_LOG_WARN, "diagnostics.connection", "%s in %lld ms %s %s",
            akeno_status_name(status), elapsed, tls, status == AKENO_OK ? "" : err);
}

#ifdef AKENO_EXPERIMENTAL_AUDIO
/* ---- Audio lab (experimental builds only) -------------------------------------------------- */

static void respond_audio_status(ui_response *r) {
    akeno_buf_append_str(&r->body, "{\"ok\":true,\"audio\":");
    audio_lab_status_json(&r->body);
    akeno_buf_append_str(&r->body, "}");
    respond_json(r, 200);
}

/*
 * POST /api/diagnostics/audio/tone {"confirm":"play","preset":"stereo16-1024","level":"moderate","delaySeconds":0}:
 * the explicit confirmation is required; presets and levels are the ones audio_lab.c offers.
 */
static void route_audio_tone(akeno_app *app, const akeno_json *root, ui_response *r) {
    const char *confirm = akeno_json_get_string(root, "confirm", NULL);
    if (!confirm || strcmp(confirm, "play") != 0) {
        respond_error(r, 400, "CONFIRMATION_REQUIRED", "Confirm the experimental test tone first.");
        return;
    }
    int64_t delay = 0;
    const akeno_json *delay_json = akeno_json_get(root, "delaySeconds");
    if (delay_json && akeno_json_int64(delay_json, &delay) != 0) delay = -1;
    char err[160] = "";
    int rc = audio_lab_tone_start(akeno_json_get_string(root, "preset", NULL), akeno_json_get_string(root, "level", NULL), (int)delay, err,
                                  sizeof(err));
    if (rc != 0) {
        respond_error(r, rc == -1 ? 400 : rc == -2 ? 500 : rc == 3 ? 429 : 409,
                      rc == -1 ? "INVALID" : rc == 1 ? "BUSY" : rc == 2 ? "TOO_SOON" : rc == 3 ? "LIMIT" : "THREAD", err);
        if (rc == -2) app_log(app, AKENO_LOG_WARN, "audio.tone", "The experimental test tone could not start: %s", err);
        return;
    }
    app_log(app, AKENO_LOG_INFO, "audio.tone", "Experimental test tone started by the user (preset %s, level %s, delay %d s).",
            akeno_json_get_string(root, "preset", "default"), akeno_json_get_string(root, "level", "default"), (int)delay);
    respond_audio_status(r);
}

/* POST /api/diagnostics/audio/feedback {"run":N,"heard":true|false,"route":"tv"}: the user's own report. */
static void route_audio_feedback(akeno_app *app, const akeno_json *root, ui_response *r) {
    int64_t run = 0;
    const akeno_json *heard = akeno_json_get(root, "heard");
    const char *route = akeno_json_get_string(root, "route", "");
    static const char *const routes[] = { "", "tv", "controller-headset", "usb-headset", "other" };
    int route_ok = 0;
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) route_ok |= strcmp(route, routes[i]) == 0;
    if (!root || akeno_json_int64(akeno_json_get(root, "run"), &run) != 0 || !heard ||
        heard->type != AKENO_JSON_BOOL || !route_ok) {
        respond_error(r, 400, "INVALID_BODY", "Expected {\"run\": number, \"heard\": boolean, \"route\": string}.");
        return;
    }
    int yes = heard->boolean != 0;
    if (audio_lab_feedback((int)run, yes, route) != 0) {
        respond_error(r, 404, "UNKNOWN_RUN", "That test is unknown or still running.");
        return;
    }
    app_log(app, AKENO_LOG_INFO, "audio.feedback", "User report for test %d: %s%s%s.", (int)run, yes ? "heard" : "not heard",
            yes && route[0] ? " on " : "", yes ? route : "");
    respond_audio_status(r);
}

/* POST /api/diagnostics/audio/browser: the page's own Web Audio test, reported for comparison. */
static void route_audio_browser(akeno_app *app, const akeno_json *root, ui_response *r) {
    if (!root || root->type != AKENO_JSON_OBJECT) {
        respond_error(r, 400, "INVALID_BODY", "Expected a JSON object.");
        return;
    }
    audio_lab_browser_result result;
    memset(&result, 0, sizeof(result));
    result.api_available = akeno_json_get_bool(root, "apiAvailable", 0);
    app_copy_str(result.state, sizeof(result.state), akeno_json_get_string(root, "state", ""));
    int64_t rate = 0;
    if (akeno_json_int64(akeno_json_get(root, "sampleRate"), &rate) == 0 && rate > 0 && rate < 1000000) result.sample_rate = (int)rate;
    result.clock_advanced = akeno_json_get_bool(root, "clockAdvanced", 0);
    const akeno_json *heard = akeno_json_get(root, "heard");
    result.heard = heard && heard->type == AKENO_JSON_BOOL ? (heard->boolean ? 1 : 0) : -1;
    const char *route = akeno_json_get_string(root, "route", "");
    app_copy_str(result.route, sizeof(result.route),
                 strcmp(route, "tv") == 0 || strcmp(route, "controller-headset") == 0 || strcmp(route, "usb-headset") == 0 ||
                         strcmp(route, "other") == 0
                     ? route
                     : "");
    app_copy_str(result.error, sizeof(result.error), akeno_json_get_string(root, "error", ""));
    for (char *c = result.state; *c; c++)
        if ((unsigned char)*c < 0x20) *c = ' ';
    for (char *c = result.error; *c; c++)
        if ((unsigned char)*c < 0x20) *c = ' ';
    audio_lab_browser_report(&result);
    app_log(app, AKENO_LOG_INFO, "audio.browser", "Browser audio test: API %s, state %s, heard %d.", result.api_available ? "available" : "missing",
            result.state, result.heard);
    respond_audio_status(r);
}
#endif

/* ---- /t/<ticket>: one-time links from notification buttons ------------------------------- */

static void route_ticket(akeno_app *app, const char *id, ui_response *r) {
    char open[APP_OPEN_MAX];
    if (app_ticket_redeem(app, id, open, sizeof(open)) != 0 || !app_valid_open(open)) {
        r->status = 403;
        r->content_type = "text/plain; charset=utf-8";
        akeno_buf_append_str(&r->body, "This AKENO NETWORK link has expired or was already used. Start AKENO NETWORK again to open its page.\n");
        app_log(app, AKENO_LOG_WARN, "ui.ticket_rejected", "A notification link was used after expiry, twice, or was invalid.");
        return;
    }
    /* The page secret goes only into the fragment of a same-origin redirect, as when AKENO opens the page. */
    r->status = 302;
    akeno_buf_printf(&r->location, "/#t=%s%s%s", app->session_token, open[0] ? "&open=" : "", open);
    app_log(app, AKENO_LOG_INFO, "ui.ticket_redeemed", "Opened the page from a notification (%s).", open[0] ? open : "home");
}

/* ---- Dispatcher --------------------------------------------------------------------------- */

static int is(const ui_request *req, const char *method, const char *path) {
    return strcmp(req->method, method) == 0 && strcmp(req->path, path) == 0;
}

void app_route(akeno_app *app, const ui_request *req, const char *body, size_t body_len, ui_response *r) {
    app_tick(app);
    /* Static page: no secret needed, contains no data. */
    if (strcmp(req->method, "GET") == 0 && (strcmp(req->path, "/") == 0 || strcmp(req->path, "/index.html") == 0)) {
        r->status = 200;
        r->content_type = "text/html; charset=utf-8";
        r->static_body = akeno_ui_index;
        r->static_len = (size_t)(akeno_ui_index_end - akeno_ui_index) - 1;
        return;
    }
    if (is(req, "GET", "/app.js")) {
        r->status = 200;
        r->content_type = "text/javascript; charset=utf-8";
        r->static_body = akeno_ui_js;
        r->static_len = (size_t)(akeno_ui_js_end - akeno_ui_js) - 1;
        return;
    }
    if (is(req, "GET", "/app.css")) {
        r->status = 200;
        r->content_type = "text/css; charset=utf-8";
        r->static_body = akeno_ui_css;
        r->static_len = (size_t)(akeno_ui_css_end - akeno_ui_css) - 1;
        return;
    }
    if (is(req, "GET", "/audiolab.js")) {
        r->status = 200;
        r->content_type = "text/javascript; charset=utf-8";
#ifdef AKENO_EXPERIMENTAL_AUDIO
        r->static_body = akeno_ui_audiolab_js;
        r->static_len = (size_t)(akeno_ui_audiolab_js_end - akeno_ui_audiolab_js) - 1;
#else
        static const char none[] = "/* AKENO NETWORK: the audio lab is not part of this build. */\n";
        r->static_body = none;
        r->static_len = sizeof(none) - 1;
#endif
        return;
    }
    if (is(req, "GET", "/favicon.ico")) {
        r->status = 204;
        return;
    }
    if (is(req, "GET", "/api/instance")) {
        respond_json(r, 200);
        akeno_buf_append_str(&r->body, "{\"app\":\"akeno-network-console\",\"version\":\"" AKENO_APP_VERSION "\"}");
        return;
    }
    if (is(req, "POST", "/api/instance/show")) {
        if (!ui_http_secret_equal(req->instance_key, app->instance_key)) {
            respond_error(r, 403, "FORBIDDEN", "Not allowed.");
            return;
        }
        /* A second launch reopens the page at the newest message the page has not shown yet. */
        char open[APP_OPEN_MAX];
        app_latest_message_target(app, open, sizeof(open));
        app_open_ui_at(app, open);
        respond_ok(r);
        return;
    }
    if (strcmp(req->method, "GET") == 0 && strncmp(req->path, "/t/", 3) == 0) {
        route_ticket(app, req->path + 3, r);
        return;
    }
    if (strncmp(req->path, "/api/", 5) != 0) {
        respond_error(r, 404, "NOT_FOUND", "Not found.");
        return;
    }
    if (!ui_http_secret_equal(req->token, app->session_token)) {
        respond_error(r, 403, "SESSION_REQUIRED", "This page has expired. Start AKENO NETWORK again to open a new one.");
        return;
    }
    if (strcmp(req->method, "GET") != 0 && body_len > 0 && strncmp(req->content_type, "application/json", 16) != 0) {
        respond_error(r, 415, "JSON_REQUIRED", "Send JSON.");
        return;
    }

    akeno_json_doc *doc = NULL;
    if (strncmp(req->path, "/api/v1/", 8) == 0) {
        route_proxy(app, req, body, body_len, r);
    } else if (is(req, "GET", "/api/media")) {
        route_media(app, req, r);
    } else if (is(req, "GET", "/api/local-art")) {
        route_local_art(app, req, r);
    } else if (is(req, "GET", "/api/state")) {
        route_state(app, r);
    } else if (is(req, "GET", "/api/events")) {
        route_events(app, req, r);
    } else if (is(req, "GET", "/api/inbox")) {
        route_inbox(app, r);
    } else if (is(req, "POST", "/api/inbox/seen")) {
        doc = akeno_json_parse(body ? body : "", body_len, NULL, 0);
        route_inbox_seen(app, akeno_json_root(doc), r);
    } else if (is(req, "POST", "/api/settings")) {
        doc = akeno_json_parse(body ? body : "", body_len, NULL, 0);
        route_settings(app, akeno_json_root(doc), r);
    } else if (is(req, "POST", "/api/pair/start")) {
        route_pair_start(app, r);
    } else if (is(req, "POST", "/api/pair/cancel")) {
        app_pair_cancel(app);
        respond_ok(r);
    } else if (is(req, "POST", "/api/logout")) {
        route_logout(app, r);
    } else if (is(req, "POST", "/api/realtime/reconnect")) {
        pthread_mutex_lock(&app->lock);
        app->rt_wanted = 1;
        pthread_mutex_unlock(&app->lock);
        app_tick(app);
        respond_ok(r);
    } else if (is(req, "GET", "/api/quick-access")) {
        respond_json(r, 200);
        akeno_buf_append_str(&r->body, "{\"ok\":true,\"quickAccess\":");
        pthread_mutex_lock(&app->lock);
        app_quick_access_json(app, &r->body);
        pthread_mutex_unlock(&app->lock);
        akeno_buf_append_str(&r->body, "}");
    } else if (is(req, "POST", "/api/quick-access/test")) {
        doc = akeno_json_parse(body ? body : "", body_len, NULL, 0);
        int64_t seconds = 30;
        if (doc) akeno_json_int64(akeno_json_get(akeno_json_root(doc), "seconds"), &seconds);
        app_quick_access_test(app, (int)(seconds > 0 && seconds <= 60 ? seconds : 30));
        respond_json(r, 200);
        akeno_buf_append_str(&r->body, "{\"ok\":true,\"quickAccess\":");
        pthread_mutex_lock(&app->lock);
        app_quick_access_json(app, &r->body);
        pthread_mutex_unlock(&app->lock);
        akeno_buf_append_str(&r->body, "}");
    } else if (is(req, "GET", "/api/diagnostics")) {
        route_diagnostics(app, r);
    } else if (is(req, "POST", "/api/diagnostics/connection")) {
        route_connection_test(app, r);
    } else if (is(req, "POST", "/api/diagnostics/actionable-test")) {
        int rc = app_test_actionable(app);
        respond_json(r, 200);
        akeno_buf_printf(&r->body, "{\"ok\":%s,\"result\":%d", rc == 0 ? "true" : "false", rc);
        json_kv_str(&r->body, "status", akeno_platform_actionable_status(), 1);
        akeno_buf_append_str(&r->body, "}");
#ifdef AKENO_EXPERIMENTAL_AUDIO
    } else if (is(req, "GET", "/api/diagnostics/audio")) {
        respond_audio_status(r);
    } else if (is(req, "POST", "/api/diagnostics/audio/probe")) {
        audio_lab_probe();
        app_log(app, AKENO_LOG_INFO, "audio.probe", "Experimental audio probe run by the user.");
        respond_audio_status(r);
    } else if (is(req, "POST", "/api/diagnostics/audio/tone")) {
        doc = akeno_json_parse(body ? body : "", body_len, NULL, 0);
        route_audio_tone(app, akeno_json_root(doc), r);
    } else if (is(req, "POST", "/api/diagnostics/audio/cancel")) {
        if (audio_lab_cancel() == 0) app_log(app, AKENO_LOG_INFO, "audio.cancel", "Experimental test cancelled by the user.");
        respond_audio_status(r);
    } else if (is(req, "POST", "/api/diagnostics/audio/feedback")) {
        doc = akeno_json_parse(body ? body : "", body_len, NULL, 0);
        route_audio_feedback(app, akeno_json_root(doc), r);
    } else if (is(req, "POST", "/api/diagnostics/audio/browser")) {
        doc = akeno_json_parse(body ? body : "", body_len, NULL, 0);
        route_audio_browser(app, akeno_json_root(doc), r);
    } else if (is(req, "GET", "/api/diagnostics/audio/report")) {
        r->status = 200;
        r->content_type = "text/plain; charset=utf-8";
        audio_lab_report_text(&r->body);
#endif
    } else if (is(req, "POST", "/api/exit")) {
        app_log(app, AKENO_LOG_INFO, "app.exit_requested", "Exit requested from the console page.");
        app->stop = 1;
        respond_ok(r);
    } else {
        respond_error(r, 404, "NOT_FOUND", "Not found.");
    }
    akeno_json_free(doc);
    (void)app_exit_requested;
}
