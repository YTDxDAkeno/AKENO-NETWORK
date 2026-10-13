#include <stdio.h>
#include <string.h>

#include "backoff.h"
#include "client_internal.h"
#include "crypto_util.h"
#include "transport.h"
#include "ws.h"

/* A connection that stayed up this long resets the failure counter. */
#define HEALTHY_SESSION_MS 30000
/* Close codes defined by the AKENO realtime protocol (docs/API.md). */
#define CLOSE_SESSION_REVOKED 4401
#define CLOSE_PROTOCOL_ERROR 4400
#define CLOSE_SERVER_RESTART 1012

static void state(const akeno_realtime_options *o, const char *name, const char *detail) {
    if (o->on_state) o->on_state(o->user, name, detail);
}

static int stopped(const akeno_realtime_options *o) { return o->should_stop && o->should_stop(o->user); }

static double random_unit(void) {
    uint32_t r = 0;
    if (akeno_random_bytes(&r, sizeof(r)) != 0) return 0.5;
    return (double)r / 4294967296.0;
}

/* Runs one connected session. Returns why it ended. *healthy is set if it was stable. */
static akeno_status run_session(akeno_client *client, akeno_ws *ws, const akeno_realtime_options *o, int *healthy) {
    long long started = akeno_now_ms();
    long long last_ping = started;
    long long ping_sent_at = 0;
    int awaiting_pong = 0;
    int hello = 0;
    for (;;) {
        if (stopped(o)) return AKENO_ERR_STOPPED;
        long long now = akeno_now_ms();
        if (hello && now - started >= HEALTHY_SESSION_MS) *healthy = 1;
        if (!awaiting_pong && now - last_ping >= o->heartbeat_interval_ms) {
            static const char ping[] = "{\"v\":1,\"type\":\"ping\"}";
            akeno_status s = akeno_ws_send_text(ws, ping, sizeof(ping) - 1);
            if (s != AKENO_OK) return s;
            awaiting_pong = 1;
            ping_sent_at = now;
            last_ping = now;
        }
        if (awaiting_pong && now - ping_sent_at > o->pong_timeout_ms) {
            akeno_log(client, AKENO_LOG_WARN, "realtime.pong_timeout", "No heartbeat reply within %d ms.", o->pong_timeout_ms);
            return AKENO_ERR_TIMEOUT;
        }

        const char *text = NULL;
        size_t len = 0;
        akeno_status s = akeno_ws_read_message(ws, 250, &text, &len);
        if (s == AKENO_ERR_TIMEOUT) continue;
        if (s != AKENO_OK) {
            if (ws->close_code == CLOSE_SESSION_REVOKED) return AKENO_ERR_UNAUTHORIZED;
            if (ws->close_code == CLOSE_PROTOCOL_ERROR) return AKENO_ERR_PROTOCOL;
            return s;
        }

        char err[96];
        akeno_json_doc *doc = akeno_json_parse(text, len, err, sizeof(err));
        const akeno_json *root = akeno_json_root(doc);
        int64_t version = 0;
        const char *type = akeno_json_get_string(root, "type", NULL);
        if (!doc || akeno_json_int64(akeno_json_get(root, "v"), &version) != 0 || version != 1 || !type) {
            akeno_log(client, AKENO_LOG_WARN, "realtime.invalid_event", "Ignoring a malformed realtime event%s%s.",
                      doc ? "" : ": ", doc ? "" : err);
            akeno_json_free(doc);
            continue;
        }
        if (strcmp(type, "pong") == 0) {
            awaiting_pong = 0;
        } else {
            if (strcmp(type, "hello") == 0 && !hello) {
                hello = 1;
                state(o, "connected", "Receiving live updates. Refresh lists now to catch up on anything missed.");
            }
            if (o->on_event) o->on_event(o->user, type, akeno_json_get(root, "data"));
        }
        akeno_json_free(doc);
    }
}

akeno_status akeno_realtime_run(akeno_client *client, const akeno_realtime_options *options) {
    if (!client || !options) return AKENO_ERR_ARGUMENT;
    akeno_realtime_options o = *options;
    if (o.max_consecutive_failures <= 0) o.max_consecutive_failures = 20;
    if (o.backoff_base_ms <= 0) o.backoff_base_ms = 1000;
    if (o.backoff_max_ms <= 0) o.backoff_max_ms = 60000;
    if (o.heartbeat_interval_ms <= 0) o.heartbeat_interval_ms = 25000;
    if (o.pong_timeout_ms <= 0) o.pong_timeout_ms = 10000;
    if (!client->paired) return AKENO_ERR_NOT_PAIRED;

    char headers[160];
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", client->token);
    int failures = 0;
    akeno_status result;
    for (;;) {
        if (stopped(&o)) { result = AKENO_ERR_STOPPED; break; }
        state(&o, "connecting", "Connecting to the realtime service.");
        char err[256] = "";
        akeno_conn *conn = NULL;
        akeno_status s = akeno_conn_open(&client->url, client->ca_file, client->connect_timeout_ms, &conn, err, sizeof(err));
        int healthy = 0;
        if (s == AKENO_OK) {
            akeno_ws ws;
            akeno_ws_init(&ws, conn, client->io_timeout_ms);
            int http_status = 0;
            s = akeno_ws_handshake(&ws, &client->url, "/ws", headers, client->io_timeout_ms, &http_status);
            if (s == AKENO_OK) {
                s = run_session(client, &ws, &o, &healthy);
                if (s == AKENO_ERR_STOPPED) akeno_ws_send_close(&ws, 1000);
                snprintf(err, sizeof(err), "Connection ended (%s, close code %d).", akeno_status_name(s), ws.close_code);
                if (ws.close_code == CLOSE_SERVER_RESTART) healthy = 1; /* Planned restart: reconnect promptly. */
            } else {
                snprintf(err, sizeof(err), "Handshake failed (%s, HTTP %d).", akeno_status_name(s), http_status);
            }
            akeno_ws_free(&ws);
            akeno_conn_close(conn);
        }
        if (s == AKENO_ERR_STOPPED) { result = s; break; }
        if (s == AKENO_ERR_UNAUTHORIZED) {
            memset(headers, 0, sizeof(headers));
            akeno_log(client, AKENO_LOG_WARN, "realtime.unpaired", "The server signed this device out. Pair it again.");
            result = s;
            break;
        }
        if (s == AKENO_ERR_FORBIDDEN || s == AKENO_ERR_PROTOCOL) {
            akeno_log(client, AKENO_LOG_ERROR, "realtime.fatal", "%s Not reconnecting.", err);
            result = s;
            break;
        }
        failures = healthy ? 0 : failures + 1;
        state(&o, "disconnected", err);
        akeno_log(client, AKENO_LOG_WARN, "realtime.disconnected", "%s", err);
        if (failures >= o.max_consecutive_failures) {
            result = AKENO_ERR_GAVE_UP;
            break;
        }
        int delay = akeno_backoff_delay_ms(failures, o.backoff_base_ms, o.backoff_max_ms, random_unit());
        akeno_log(client, AKENO_LOG_INFO, "realtime.reconnect_scheduled", "Reconnecting in %d ms (attempt %d of %d).", delay,
                  failures + 1, o.max_consecutive_failures);
        long long until = akeno_now_ms() + delay;
        while (akeno_now_ms() < until && !stopped(&o)) {
            long long left = until - akeno_now_ms();
            akeno_sleep_ms(left > 200 ? 200 : (int)left);
        }
    }
    memset(headers, 0, sizeof(headers));
    state(&o, "stopped", akeno_status_name(result));
    return result;
}
