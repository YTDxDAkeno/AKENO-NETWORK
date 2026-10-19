/*
 * AKENO Party Voice: the network thread of a session. The same steps as AKENO Native's voice
 * (clients/ps5-native/src/app/voice_app.c): a voice pass from the AKENO server through the daemon's
 * native interface (IPC 3), the gateway must be on the server's own host, TLS WebSocket with the
 * daemon's CA, hello, control messages, Opus frames both ways, reconnects with a new pass.
 * The voice pass never leaves this thread's memory and is wiped after use; it is never logged.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "account.h"
#include "akeno/json.h"
#include "buf.h"
#include "crypto_util.h"
#include "ipc.h"
#include "pv_internal.h"
#include "social.h"
#include "transport.h"
#include "url.h"
#include "voice.h"
#include "ws.h"

#define PING_MS 2000
#define SILENT_LIMIT_MS 15000   /* nothing from the gateway for this long: the connection is lost */
#define REQUEST_TIMEOUT_MS 12000
#define CONNECT_TIMEOUT_MS 8000
#define READY_TIMEOUT_MS 8000
#define FRAME_MAX 65536         /* largest gateway frame accepted */
#define CLIENT_NAME PV_APP "/" PV_VERSION

static void set_net(pv_engine *e, pv_net_state state, const char *error) {
    pthread_mutex_lock(&e->lock);
    pv_net_state before = e->st.net;
    e->st.net = state;
    if (error) snprintf(e->st.net_error, sizeof(e->st.net_error), "%s", error);
    else if (state == PV_NET_CONNECTED || state == PV_NET_REQUESTING) e->st.net_error[0] = '\0';
    if (state != PV_NET_CONNECTED) {
        if (e->st.connected_since_ms) e->st.connected_total_ms += pv_now_ms(e) - e->st.connected_since_ms;
        e->st.connected_since_ms = 0;
        e->st.people_count = 0;
    }
    e->report_dirty = 1;
    pthread_mutex_unlock(&e->lock);
    if (before != state) pv_log(e, "voice.state", "%s%s%s", pv_net_state_name(state), error ? ": " : "", error ? error : "");
}

/* ---- The voice pass, through the daemon (IPC 3) ---------------------------------------------- */

static int daemon_interface(pv_engine *e, int *ipc) {
    akn_http_reply reply;
    akn_loopback_get(e->cfg.daemon_port, "/api/instance", NULL, 3000, 4096, &reply);
    char version[32];
    int rc = !reply.transport_error && reply.status == 200 && reply.body ? akn_parse_daemon_instance(reply.body, reply.len, version, sizeof(version), ipc) : -1;
    akn_http_reply_free(&reply);
    return rc;
}

/* 0 with the credential, else -1 with a sentence; *retry says whether trying again may help. */
static int request_credential(pv_session *s, akn_voice_credential *cred, char *err, size_t err_len, int *retry) {
    pv_engine *e = s->e;
    *retry = 0;
    int ipc = 0;
    if (daemon_interface(e, &ipc) != 0) {
        pthread_mutex_lock(&e->lock);
        e->st.daemon_ipc = -1;
        pthread_mutex_unlock(&e->lock);
        snprintf(err, err_len, "The AKENO daemon is not answering on port %d.", e->cfg.daemon_port);
        *retry = 1;
        return -1;
    }
    pthread_mutex_lock(&e->lock);
    e->st.daemon_ipc = ipc;
    pthread_mutex_unlock(&e->lock);
    if (ipc < 3) {
        snprintf(err, err_len, "This AKENO daemon has native interface %d; voice needs 3 (the native-IPC daemon 196ca13c71d1 or newer).", ipc);
        return -1;
    }
    char key_path[300], key[AKN_NATIVE_KEY_LEN + 1], path[160];
    snprintf(key_path, sizeof(key_path), "%s/native.key", e->cfg.akeno_dir);
    if (akn_read_native_key(key_path, key, sizeof(key)) != 0) {
        snprintf(err, err_len, "The AKENO daemon's native key is missing. Restart the daemon.");
        *retry = 1;
        return -1;
    }
    snprintf(path, sizeof(path), "/api/v1/parties/%s/voice/device-token", s->party_id);
    akn_http_reply reply;
    akn_loopback_request(e->cfg.daemon_port, "POST", path, key, "{}", REQUEST_TIMEOUT_MS, 16384, &reply);
    memset(key, 0, sizeof(key));
    if (reply.transport_error || !reply.body) {
        akn_http_reply_free(&reply);
        snprintf(err, err_len, "The AKENO daemon is not answering.");
        *retry = 1;
        return -1;
    }
    akn_proxy_result res;
    memset(&res, 0, sizeof(res));
    const akeno_json *data = NULL;
    akeno_json_doc *doc = akn_proxy_parse(reply.body, reply.len, &res, &data);
    if (reply.len) memset(reply.body, 0, reply.len); /* the voice pass */
    akn_http_reply_free(&reply);
    int rc = -1;
    if (!doc) {
        snprintf(err, err_len, "The daemon sent an answer the voice service cannot read.");
    } else if (!res.ok) {
        if (strcmp(res.code, "NOT_ALLOWED_FOR_NATIVE") == 0 || strcmp(res.code, "NOT_ALLOWED") == 0)
            snprintf(err, err_len, "This AKENO daemon cannot ask for voice. Run the native-IPC daemon (interface 3).");
        else if (strcmp(res.code, "NOT_PAIRED") == 0 || strcmp(res.code, "UNPAIRED") == 0)
            snprintf(err, err_len, "This console is not paired. Pair it on the AKENO page first.");
        else if (strcmp(res.code, "DEVICE_SCOPE_DENIED") == 0)
            snprintf(err, err_len, "This console may not use parties. Allow \"parties\" for it on the AKENO website.");
        else if (res.status == 404)
            snprintf(err, err_len, "The AKENO server has no console voice yet (voice pass: HTTP 404). The server needs the voice upgrade.");
        else if (strcmp(akn_voice_error_text(res.code), "Voice failed.") != 0)
            snprintf(err, err_len, "%s", akn_voice_error_text(res.code));
        else if (res.message[0])
            snprintf(err, err_len, "%s", res.message);
        else
            snprintf(err, err_len, "The AKENO server answered %d.", res.status);
        *retry = res.status == 0 || (res.status >= 500 && strcmp(res.code, "VOICE_GATEWAY_UNAVAILABLE") != 0) || res.status == 429;
        pv_log(e, "voice.pass", "refused: HTTP %d %s", res.status, res.code[0] ? res.code : "-");
    } else if (akn_voice_credential_from(data, cred) != 0) {
        snprintf(err, err_len, "The voice pass from the AKENO server is incomplete (it must point to a wss:// gateway).");
    } else {
        rc = 0;
    }
    akeno_json_free(doc);
    return rc;
}

/* ---- WebSocket frames (binary and text) on top of the client library's handshake ------------- */

static akeno_status send_frame(akeno_ws *ws, int opcode, const void *payload, size_t len) {
    uint8_t mask[4];
    if (akeno_random_bytes(mask, sizeof(mask)) != 0) return AKENO_ERR_TLS;
    akeno_buf frame;
    akeno_buf_init(&frame, len + 16);
    if (akeno_ws_encode(&frame, opcode, payload, len, mask) != 0) {
        akeno_buf_free(&frame);
        return AKENO_ERR_NO_MEMORY;
    }
    akeno_status status = akeno_conn_write_all(ws->conn, frame.data, frame.len, 3000);
    akeno_buf_free(&frame);
    return status;
}

static akeno_status send_text(akeno_ws *ws, const char *text) { return send_frame(ws, 1, text, strlen(text)); }

typedef struct {
    int opcode;        /* 1 text, 2 binary, 8 close, 9 ping */
    uint8_t *payload;  /* malloc'd, NUL-terminated */
    size_t len;
} ws_msg;

/* 1 with a frame, 0 when none is complete yet, -1 when the connection failed or the gateway broke
 * the protocol (*close_code says why, 1006 when unknown). */
static int next_frame(akeno_ws *ws, int wait_ms, ws_msg *out, int *close_code) {
    for (int pass = 0; pass < 2; pass++) {
        akeno_ws_frame h;
        int got = ws->in.len ? akeno_ws_parse_header((const uint8_t *)ws->in.data, ws->in.len, &h) : 0;
        if (got < 0 || (got == 1 && (h.masked || !h.fin || h.payload_len > FRAME_MAX))) {
            *close_code = 1002;
            return -1;
        }
        if (got == 1 && ws->in.len >= h.header_len + h.payload_len) {
            out->opcode = h.opcode;
            out->len = (size_t)h.payload_len;
            out->payload = malloc(out->len + 1);
            if (!out->payload) {
                *close_code = 1011;
                return -1;
            }
            memcpy(out->payload, ws->in.data + h.header_len, out->len);
            out->payload[out->len] = 0;
            akeno_buf_consume(&ws->in, h.header_len + out->len);
            return 1;
        }
        if (pass == 1) break;
        int ready = akeno_conn_wait_readable(ws->conn, wait_ms);
        if (ready < 0) {
            *close_code = 1006;
            return -1;
        }
        if (ready == 0) return 0;
        uint8_t tmp[8192];
        long n = akeno_conn_read(ws->conn, tmp, sizeof(tmp), 100);
        if (n == -(long)AKENO_ERR_TIMEOUT) continue;
        if (n <= 0) {
            *close_code = 1006;
            return -1;
        }
        if (akeno_buf_append(&ws->in, tmp, (size_t)n) != 0) {
            *close_code = 1009;
            return -1;
        }
    }
    return 0;
}

/* ---- One connection --------------------------------------------------------------------------- */

static void handle_text(pv_session *s, const char *text, size_t len, int *ready, char *err, size_t err_len) {
    pv_engine *e = s->e;
    akn_voice_msg *m = malloc(sizeof(*m));
    if (!m) return;
    if (akn_voice_parse(text, len, m) == 0) {
        switch (m->type) {
        case AKN_VMSG_READY:
            if (akn_voice_ready_ok(m)) *ready = 1;
            else snprintf(err, err_len, "The voice gateway speaks a different protocol. Update the voice service.");
            break;
        case AKN_VMSG_PARTICIPANTS:
            pthread_mutex_lock(&e->lock);
            e->st.people_count = 0;
            for (int i = 0; i < m->count && i < AKN_VOICE_MAX_PARTICIPANTS; i++) {
                e->st.people[e->st.people_count++] = m->list[i];
                if (m->list[i].self && m->list[i].level > e->st.gateway_self_level_max) e->st.gateway_self_level_max = m->list[i].level;
            }
            if (!e->st.participants_seen) e->report_dirty = 1;
            e->st.participants_seen = 1;
            pthread_mutex_unlock(&e->lock);
            break;
        case AKN_VMSG_STATE:
            pthread_mutex_lock(&e->lock);
            if (e->st.net == PV_NET_CONNECTED)
                snprintf(e->st.net_error, sizeof(e->st.net_error), "%s", m->reconnecting ? "The voice server is reconnecting..." : "");
            pthread_mutex_unlock(&e->lock);
            break;
        case AKN_VMSG_ERROR:
            snprintf(err, err_len, "%s",
                     strcmp(akn_voice_error_text(m->code), "Voice failed.") != 0 ? akn_voice_error_text(m->code) : m->message[0] ? m->message : "Voice failed.");
            pv_log(e, "voice.gateway_error", "%s", m->code);
            break;
        case AKN_VMSG_PONG: {
            uint64_t now = pv_now_ms(e);
            pthread_mutex_lock(&e->lock);
            if (m->id > 0 && (uint64_t)m->id <= now) e->st.latency_ms = (int)(now - (uint64_t)m->id);
            pthread_mutex_unlock(&e->lock);
            break;
        }
        case AKN_VMSG_UNKNOWN: break;
        }
    }
    free(m);
}

/* Returns the close code (1000 when the user left). */
static int run_connection(pv_session *s, const akn_voice_credential *cred, char *err, size_t err_len) {
    pv_engine *e = s->e;
    char https[300], ca_path[300];
    snprintf(https, sizeof(https), "https://%s", cred->url + 6);
    akeno_url url;
    if (akeno_url_parse(https, &url) != 0) {
        snprintf(err, err_len, "The voice gateway address is not valid.");
        return 4000;
    }
    snprintf(ca_path, sizeof(ca_path), "%s/ca.pem", e->cfg.akeno_dir);
    struct stat st;
    const char *ca = stat(ca_path, &st) == 0 && S_ISREG(st.st_mode) ? ca_path : NULL; /* as the daemon */
    akeno_transport_init();
    akeno_conn *conn = NULL;
    char why[160] = "";
    if (akeno_conn_open(&url, ca, CONNECT_TIMEOUT_MS, &conn, why, sizeof(why)) != AKENO_OK) {
        snprintf(err, err_len, "The voice gateway could not be reached.");
        pv_log(e, "voice.connect", "TLS connection failed: %s", why);
        return 1006;
    }
    akeno_ws ws;
    akeno_ws_init(&ws, conn, 3000);
    int http_status = 0;
    akeno_status hs = akeno_ws_handshake(&ws, &url, "", NULL, CONNECT_TIMEOUT_MS, &http_status);
    if (hs != AKENO_OK) {
        snprintf(err, err_len, "The voice gateway refused the connection (HTTP %d).", http_status);
        pv_log(e, "voice.connect", "WebSocket handshake failed: HTTP %d (status %d)", http_status, (int)hs);
        akeno_ws_free(&ws);
        akeno_conn_close(conn);
        return 1006;
    }
    pthread_mutex_lock(&e->lock);
    int muted = !pv_transmitting_locked(e), deafened = e->st.deafened;
    pthread_mutex_unlock(&e->lock);
    size_t hello_cap = AKN_VOICE_TOKEN_MAX * 2 + 256;
    char *hello = malloc(hello_cap);
    if (!hello || akn_voice_hello_json(cred->token, CLIENT_NAME, muted, deafened, hello, hello_cap) < 0 || send_text(&ws, hello) != AKENO_OK) {
        if (hello) memset(hello, 0, hello_cap);
        free(hello);
        snprintf(err, err_len, "The voice pass could not be sent.");
        akeno_ws_free(&ws);
        akeno_conn_close(conn);
        return 1006;
    }
    memset(hello, 0, hello_cap);
    free(hello);

    int ready = 0, close_code = 1006, sent_muted = muted, sent_deafened = deafened;
    uint64_t started = pv_now_ms(e), last_rx = started, last_ping = started, last_stats = started;
    err[0] = '\0';
    for (;;) {
        if (!s->run) {
            send_text(&ws, "{\"type\":\"leave\"}");
            akeno_ws_send_close(&ws, 1000);
            close_code = 1000;
            break;
        }
        uint64_t now = pv_now_ms(e);
        if (!ready && now - started > READY_TIMEOUT_MS) {
            if (!err[0]) snprintf(err, err_len, "The voice gateway did not answer.");
            break;
        }
        if (ready && now - last_rx > SILENT_LIMIT_MS) {
            snprintf(err, err_len, "The voice connection went silent.");
            break;
        }
        if (ready) {
            /* control messages and microphone packets out */
            pv_tx_packet out[PV_TX_QUEUE];
            int n = 0;
            pthread_mutex_lock(&s->lock);
            while (s->tx_count > 0) {
                out[n++] = s->tx[s->tx_head];
                s->tx_head = (s->tx_head + 1) % PV_TX_QUEUE;
                s->tx_count--;
            }
            pthread_mutex_unlock(&s->lock);
            pthread_mutex_lock(&e->lock);
            int m = !pv_transmitting_locked(e), d = e->st.deafened;
            pthread_mutex_unlock(&e->lock);
            char msg[64];
            akeno_status ok = AKENO_OK;
            if (m != sent_muted && akn_voice_flag_json("mute", "muted", m, msg, sizeof(msg)) > 0 && (ok = send_text(&ws, msg)) == AKENO_OK) sent_muted = m;
            if (ok == AKENO_OK && d != sent_deafened && akn_voice_flag_json("deafen", "deafened", d, msg, sizeof(msg)) > 0 &&
                (ok = send_text(&ws, msg)) == AKENO_OK)
                sent_deafened = d;
            unsigned long sent = 0, sent_nonzero = 0;
            for (int i = 0; i < n && ok == AKENO_OK; i++) {
                if (m) continue; /* muted since it was queued: never sent */
                ok = send_frame(&ws, 2, out[i].data, out[i].len);
                if (ok == AKENO_OK) {
                    sent++;
                    sent_nonzero += out[i].nonzero != 0;
                }
            }
            if (ok == AKENO_OK && now - last_ping >= PING_MS) {
                last_ping = now;
                if (akn_voice_ping_json((long long)now, msg, sizeof(msg)) > 0) ok = send_text(&ws, msg);
            }
            pthread_mutex_lock(&e->lock);
            e->st.sent += sent;
            e->st.sent_nonzero += sent_nonzero;
            pthread_mutex_unlock(&e->lock);
            if (now - last_stats >= 10000) {
                last_stats = now;
                pv_status v;
                pv_engine_status(e, &v);
                pv_log(e, "voice.stats",
                       "sent=%lu sent_nonzero=%lu received=%lu decoded=%lu concealed=%lu rebuffers=%lu out_calls=%lu out_errors=%lu paced=%d "
                       "rx_level=%d rx_tone=%d mic=%s mic_frames=%lu mic_nonzero=%lu mic_level=%d tx_tone=%d muted=%d deafened=%d rtt=%d people=%d game=%d",
                       v.sent, v.sent_nonzero, v.received, v.decoded, v.concealed, v.underruns, v.out_calls, v.out_errors, v.paced, v.rx_level,
                       v.rx_tone_hz, pv_mic_state_name(v.mic), v.mic_frames, v.mic_nonzero_frames, v.mic_level, v.tx_tone_hz, v.muted, v.deafened,
                       v.latency_ms, v.people_count, v.game_running);
            }
            if (ok != AKENO_OK) {
                snprintf(err, err_len, "The voice connection was lost.");
                break;
            }
        }
        ws_msg f;
        int got = next_frame(&ws, 5, &f, &close_code);
        if (got < 0) {
            if (!err[0]) snprintf(err, err_len, "The voice connection was lost.");
            break;
        }
        if (got == 0) continue;
        last_rx = pv_now_ms(e);
        if (f.opcode == 1) {
            int was_ready = ready;
            handle_text(s, (const char *)f.payload, f.len, &ready, err, err_len);
            if (!was_ready && ready) {
                pthread_mutex_lock(&e->lock);
                int first = !e->st.ever_connected || e->st.connections == 0;
                int again = e->st.attempts > 0;
                e->st.attempts = 0;
                e->st.connections++;
                e->st.ready_seen = 1;
                e->st.ever_connected = 1;
                e->st.connected_since_ms = pv_now_ms(e);
                if (again) pv_note_locked(e, "AKENO Voice: reconnected to the voice chat");
                else pv_note_locked(e, "AKENO Voice: in the party's voice chat%s", first ? "" : " again");
                pthread_mutex_unlock(&e->lock);
                pthread_mutex_lock(&s->lock);
                s->connected = 1;
                akn_jitter_init(&s->jitter);
                pthread_mutex_unlock(&s->lock);
                set_net(e, PV_NET_CONNECTED, NULL);
                pv_log(e, "voice.connected", "in voice (gateway on the AKENO server's host)");
            }
        } else if (f.opcode == 2 && ready) {
            uint16_t seq;
            const uint8_t *opus;
            size_t opus_len;
            int good = akn_voice_unpack_audio(f.payload, f.len, &seq, &opus, &opus_len) == 0;
            if (good) {
                pthread_mutex_lock(&s->lock);
                akn_jitter_put(&s->jitter, seq, opus, opus_len);
                pthread_mutex_unlock(&s->lock);
            }
            pthread_mutex_lock(&e->lock);
            if (good) e->st.received++;
            else e->st.rx_malformed++;
            pthread_mutex_unlock(&e->lock);
        } else if (f.opcode == 8) {
            close_code = f.len >= 2 ? (f.payload[0] << 8) | f.payload[1] : 1005;
            akeno_ws_send_close(&ws, close_code == 1005 ? 1000 : close_code);
            free(f.payload);
            break;
        } else if (f.opcode == 9) {
            send_frame(&ws, 10, f.payload, f.len);
        }
        free(f.payload);
    }
    pthread_mutex_lock(&s->lock);
    s->connected = 0;
    s->tx_count = 0;
    pthread_mutex_unlock(&s->lock);
    akeno_ws_free(&ws);
    akeno_conn_close(conn);
    if (!err[0] || close_code >= 4000) snprintf(err, err_len, "%s", akn_voice_close_text(close_code));
    pthread_mutex_lock(&e->lock);
    e->st.close_code = close_code;
    pthread_mutex_unlock(&e->lock);
    pv_log(e, "voice.closed", "voice connection closed (code %d)", close_code);
    return close_code;
}

/* A close the session must not retry: removed, replaced, the room ended, the user left. */
static int final_close(int code) { return !akn_voice_should_retry(code, 0); }

void *pv_net_thread(void *arg) {
    pv_session *s = arg;
    pv_engine *e = s->e;
    char err[200], server[320], settings[300];
    akn_voice_credential *cred = malloc(sizeof(*cred));
    snprintf(settings, sizeof(settings), "%s/settings.json", e->cfg.akeno_dir);
    while (cred && s->run) {
        set_net(e, PV_NET_REQUESTING, NULL);
        int retry = 0, code = 0;
        err[0] = '\0';
        if (request_credential(s, cred, err, sizeof(err), &retry) != 0) {
            code = retry ? 1006 : 1000;
        } else if (akn_read_server_setting(settings, server, sizeof(server)) != 0 || !akn_voice_same_host(cred->url, server)) {
            snprintf(err, sizeof(err), "The voice gateway is not on this console's AKENO server; the voice service does not connect to it.");
            code = 1000;
        } else {
            set_net(e, PV_NET_CONNECTING, NULL);
            code = run_connection(s, cred, err, sizeof(err));
        }
        memset(cred, 0, sizeof(*cred));
        if (!s->run) break;
        pthread_mutex_lock(&e->lock);
        int attempts = ++e->st.attempts;
        if (code != 1000) e->st.reconnects++;
        pthread_mutex_unlock(&e->lock);
        if (code == 1000 || final_close(code) || attempts > PV_RECONNECT_MAX) {
            if (attempts > PV_RECONNECT_MAX && !final_close(code) && code != 1000)
                snprintf(err + strlen(err), sizeof(err) - strlen(err), " Gave up after %d attempts.", PV_RECONNECT_MAX);
            pthread_mutex_lock(&e->lock);
            pv_note_locked(e, "AKENO Voice: %s", err[0] ? err : "voice ended");
            pv_set_failure_locked(e, "%s", err[0] ? err : "voice ended");
            pthread_mutex_unlock(&e->lock);
            set_net(e, PV_NET_ENDED, err[0] ? err : "Voice ended.");
            s->run = 0;
            break;
        }
        uint32_t pause = akn_voice_backoff_ms(attempts);
        pthread_mutex_lock(&e->lock);
        if (attempts == 1) pv_note_locked(e, "AKENO Voice: connection lost, reconnecting...");
        pv_set_failure_locked(e, "%s", err[0] ? err : "connection lost");
        pthread_mutex_unlock(&e->lock);
        set_net(e, PV_NET_RECONNECTING, err);
        pv_sleep_while(&s->run, pause);
    }
    free(cred);
    s->cap_run = 0;
    s->net_done = 1;
    return NULL;
}
