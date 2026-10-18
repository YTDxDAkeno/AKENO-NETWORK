#include "voice_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "akeno/json.h"
#include "audio_lab_pcm.h"
#include "buf.h"
#include "crypto_util.h"
#include "ipc.h"
#include "jitter.h"
#include "transport.h"
#include "url.h"
#include "voice.h"
#include "voice_codec.h"
#include "voice_dsp.h"
#include "voice_proto.h"
#include "ws.h"

#define VOICE_OUT_GRAIN 1024        /* frames per output call: the hardware-verified default of Native 0.1 */
#define VOICE_MIC_GRAIN 256         /* frames per microphone read */
#define VOICE_TX_QUEUE 12           /* 240 ms of microphone packets waiting for the network */
#define VOICE_PING_MS 2000
#define VOICE_SILENT_LIMIT_MS 15000 /* nothing from the gateway for this long: the connection is lost */
#define VOICE_REQUEST_TIMEOUT_MS 12000
#define VOICE_CONNECT_TIMEOUT_MS 8000
#define VOICE_READY_TIMEOUT_MS 8000
#define VOICE_FRAME_MAX 65536       /* largest gateway frame accepted */
#define VOICE_THREAD_STACK (1024 * 1024)

typedef struct {
    uint8_t data[AKN_VOICE_MAX_PACKET + 3];
    size_t len;
} tx_packet;

typedef struct akn_voice_rt {
    akn_app *app;
    SDL_mutex *lock;
    volatile int run;
    int test;                         /* the local microphone test: no network */
    char party_id[64];
    SDL_Thread *net, *play, *cap;
    volatile int net_done, play_done, cap_done;
    /* rt->lock */
    akn_jitter jitter;
    tx_packet tx[VOICE_TX_QUEUE];
    int tx_head, tx_count;
    int connected;
    int send_mute, send_deafen;
    akn_pcm_fifo monitor;
} akn_voice_rt;

static akn_voice_rt *g_rt; /* the one session (main thread owns the pointer) */

/* ---- State helpers (app->lock) ------------------------------------------------------------------ */

static void set_state(akn_app *app, akn_voice_state state, const char *error) {
    SDL_LockMutex(app->lock);
    akn_voice_state before = app->voice.state;
    app->voice.state = state;
    if (error) snprintf(app->voice.error, sizeof(app->voice.error), "%s", error);
    else if (state == AKN_VOICE_CONNECTED) app->voice.error[0] = '\0';
    if (state == AKN_VOICE_CONNECTED) app->voice.connected_at = akn_platform_now_ms();
    if (state != AKN_VOICE_CONNECTED) {
        app->voice.people_count = 0;
        app->voice.self_speaking = 0;
    }
    SDL_UnlockMutex(app->lock);
    if (before != state)
        akn_logf(&app->log, state == AKN_VOICE_FAILED ? AKN_LOG_WARN : AKN_LOG_INFO, "voice.state", "%s%s%s",
                 state == AKN_VOICE_FAILED ? "Voice stopped" : akn_voice_state_text(&(akn_voice){ .state = state }),
                 error ? ": " : "", error ? error : "");
}

static void set_mic(akn_app *app, akn_mic_state mic, int32_t rc, const char *detail) {
    SDL_LockMutex(app->lock);
    app->voice.mic = mic;
    app->voice.mic_rc = rc;
    if (detail) snprintf(app->voice.mic_detail, sizeof(app->voice.mic_detail), "%s", detail);
    SDL_UnlockMutex(app->lock);
}

static int running(akn_voice_rt *rt) { return rt->run; }

static void sleep_while_running(akn_voice_rt *rt, uint32_t ms) {
    uint64_t end = akn_platform_now_ms() + ms;
    while (running(rt) && akn_platform_now_ms() < end) SDL_Delay(20);
}

/* ---- The voice pass, through the daemon (IPC 3) ---------------------------------------------- */

/* Returns 0 with the credential, else -1 with a readable error; *retry says whether trying again may help. */
static int request_credential(akn_voice_rt *rt, akn_voice_credential *cred, char *err, size_t err_len, int *retry) {
    akn_app *app = rt->app;
    *retry = 0;
    char key_path[300], key[AKN_NATIVE_KEY_LEN + 1], path[160];
    snprintf(key_path, sizeof(key_path), "%s/native.key", app->platform.akeno_dir);
    if (akn_read_native_key(key_path, key, sizeof(key)) != 0) {
        snprintf(err, err_len, "The AKENO daemon's native key is missing. Restart the daemon.");
        *retry = 1;
        return -1;
    }
    snprintf(path, sizeof(path), "/api/v1/parties/%s/voice/device-token", rt->party_id);
    akn_http_reply reply;
    akn_loopback_request(app->platform.daemon_port, "POST", path, key, "{}", VOICE_REQUEST_TIMEOUT_MS, 16384, &reply);
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
    akn_http_reply_free(&reply);
    int rc = -1;
    if (!doc) {
        snprintf(err, err_len, "The daemon sent an answer AKENO Native cannot read.");
    } else if (!res.ok) {
        if (strcmp(res.code, "NOT_ALLOWED_FOR_NATIVE") == 0 || strcmp(res.code, "NOT_ALLOWED") == 0)
            snprintf(err, err_len, "This AKENO daemon cannot ask for voice. Send the native-IPC daemon of this package.");
        else if (strcmp(res.code, "NOT_PAIRED") == 0 || strcmp(res.code, "UNPAIRED") == 0)
            snprintf(err, err_len, "This console is not paired. Pair it on the AKENO page first.");
        else if (strcmp(res.code, "DEVICE_SCOPE_DENIED") == 0)
            snprintf(err, err_len, "This console may not use parties. Allow \"parties\" for it on the AKENO website.");
        else if (strcmp(akn_voice_error_text(res.code), "Voice failed.") != 0)
            snprintf(err, err_len, "%s", akn_voice_error_text(res.code));
        else if (res.message[0])
            snprintf(err, err_len, "%s", res.message);
        else
            snprintf(err, err_len, "The AKENO server answered %d.", res.status);
        *retry = res.status == 0 || (res.status >= 500 && strcmp(res.code, "VOICE_GATEWAY_UNAVAILABLE") != 0) || res.status == 429;
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
    int opcode;        /* 1 text, 2 binary, 8 close */
    uint8_t *payload;  /* malloc'd, NUL-terminated */
    size_t len;
} ws_msg;

/*
 * Reads whatever is available (waiting up to wait_ms) and returns the next complete frame:
 * 1 with a frame, 0 when none is complete yet, -1 when the connection failed or the gateway broke
 * the protocol (*close_code then says why, 1006 when unknown).
 */
static int next_frame(akeno_ws *ws, int wait_ms, ws_msg *out, int *close_code) {
    for (int pass = 0; pass < 2; pass++) {
        akeno_ws_frame h;
        int got = ws->in.len ? akeno_ws_parse_header((const uint8_t *)ws->in.data, ws->in.len, &h) : 0;
        if (got < 0 || (got == 1 && (h.masked || !h.fin || h.payload_len > VOICE_FRAME_MAX))) {
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

/* ---- Network thread --------------------------------------------------------------------------- */

static void handle_text(akn_voice_rt *rt, const char *text, size_t len, int *ready, char *err, size_t err_len) {
    akn_app *app = rt->app;
    akn_voice_msg *m = malloc(sizeof(*m));
    if (!m) return;
    if (akn_voice_parse(text, len, m) == 0) {
        switch (m->type) {
        case AKN_VMSG_READY:
            if (akn_voice_ready_ok(m)) *ready = 1;
            else snprintf(err, err_len, "The voice gateway speaks a different protocol. Update AKENO Native.");
            break;
        case AKN_VMSG_PARTICIPANTS:
            SDL_LockMutex(app->lock);
            akn_voice_set_people(&app->voice, m);
            SDL_UnlockMutex(app->lock);
            break;
        case AKN_VMSG_STATE:
            SDL_LockMutex(app->lock);
            if (app->voice.state == AKN_VOICE_CONNECTED || app->voice.state == AKN_VOICE_RECONNECTING)
                snprintf(app->voice.error, sizeof(app->voice.error), "%s", m->reconnecting ? "The voice server is reconnecting..." : "");
            SDL_UnlockMutex(app->lock);
            break;
        case AKN_VMSG_ERROR:
            snprintf(err, err_len, "%s", strcmp(akn_voice_error_text(m->code), "Voice failed.") != 0 ? akn_voice_error_text(m->code)
                                         : m->message[0]                                       ? m->message
                                                                                               : "Voice failed.");
            akn_logf(&app->log, AKN_LOG_WARN, "voice.gateway_error", "%s", m->code);
            break;
        case AKN_VMSG_PONG: {
            uint64_t now = akn_platform_now_ms();
            SDL_LockMutex(app->lock);
            if (m->id > 0 && (uint64_t)m->id <= now) app->voice.latency_ms = (int)(now - (uint64_t)m->id);
            SDL_UnlockMutex(app->lock);
            break;
        }
        case AKN_VMSG_UNKNOWN: break;
        }
    }
    free(m);
}

/* One connection: returns the close code (1000 when the user left). */
static int run_connection(akn_voice_rt *rt, const akn_voice_credential *cred, char *err, size_t err_len) {
    akn_app *app = rt->app;
    char https[300], ca_path[300];
    snprintf(https, sizeof(https), "https://%s", cred->url + 6);
    akeno_url url;
    if (akeno_url_parse(https, &url) != 0) {
        snprintf(err, err_len, "The voice gateway address is not valid.");
        return 4000;
    }
    snprintf(ca_path, sizeof(ca_path), "%s/ca.pem", app->platform.akeno_dir);
    struct stat st;
    const char *ca = stat(ca_path, &st) == 0 && S_ISREG(st.st_mode) ? ca_path : NULL; /* as the daemon */
    akeno_transport_init();
    akeno_conn *conn = NULL;
    char why[160] = "";
    if (akeno_conn_open(&url, ca, VOICE_CONNECT_TIMEOUT_MS, &conn, why, sizeof(why)) != AKENO_OK) {
        snprintf(err, err_len, "The voice gateway could not be reached.");
        akn_logf(&app->log, AKN_LOG_WARN, "voice.connect", "TLS connection failed: %s", why);
        return 1006;
    }
    akeno_ws ws;
    akeno_ws_init(&ws, conn, 3000);
    int http_status = 0;
    akeno_status hs = akeno_ws_handshake(&ws, &url, "", NULL, VOICE_CONNECT_TIMEOUT_MS, &http_status);
    if (hs != AKENO_OK) {
        snprintf(err, err_len, "The voice gateway refused the connection (HTTP %d).", http_status);
        akn_logf(&app->log, AKN_LOG_WARN, "voice.connect", "WebSocket handshake failed: HTTP %d (%s)", http_status, akeno_status_name(hs));
        akeno_ws_free(&ws);
        akeno_conn_close(conn);
        return http_status == 503 ? 1013 : 1006;
    }
    char *hello = malloc(AKN_VOICE_TOKEN_MAX * 2 + 256);
    SDL_LockMutex(app->lock);
    int muted = app->voice.muted, deafened = app->voice.deafened;
    SDL_UnlockMutex(app->lock);
    int close_code = 1006;
    if (!hello || akn_voice_hello_json(cred->token, "akeno-native/" AKN_NATIVE_VERSION, muted, deafened, hello, AKN_VOICE_TOKEN_MAX * 2 + 256) < 0 ||
        send_text(&ws, hello) != AKENO_OK) {
        free(hello);
        snprintf(err, err_len, "The voice pass could not be sent.");
        akeno_ws_free(&ws);
        akeno_conn_close(conn);
        return 1006;
    }
    memset(hello, 0, AKN_VOICE_TOKEN_MAX * 2 + 256);
    free(hello);

    int ready = 0;
    uint64_t started = akn_platform_now_ms(), last_rx = started, last_ping = started;
    err[0] = '\0';
    for (;;) {
        if (!running(rt)) {
            send_text(&ws, "{\"type\":\"leave\"}");
            akeno_ws_send_close(&ws, 1000);
            close_code = 1000;
            break;
        }
        uint64_t now = akn_platform_now_ms();
        if (!ready && now - started > VOICE_READY_TIMEOUT_MS) {
            if (!err[0]) snprintf(err, err_len, "The voice gateway did not answer.");
            close_code = 1006;
            break;
        }
        if (ready && now - last_rx > VOICE_SILENT_LIMIT_MS) {
            snprintf(err, err_len, "The voice connection went silent.");
            close_code = 1006;
            break;
        }
        /* control messages and microphone packets out */
        if (ready) {
            char msg[64];
            SDL_LockMutex(rt->lock);
            int sm = rt->send_mute, sd = rt->send_deafen;
            rt->send_mute = rt->send_deafen = 0;
            tx_packet out[VOICE_TX_QUEUE];
            int n = 0;
            while (rt->tx_count > 0) {
                out[n++] = rt->tx[rt->tx_head];
                rt->tx_head = (rt->tx_head + 1) % VOICE_TX_QUEUE;
                rt->tx_count--;
            }
            SDL_UnlockMutex(rt->lock);
            SDL_LockMutex(app->lock);
            int m = app->voice.muted, d = app->voice.deafened;
            SDL_UnlockMutex(app->lock);
            akeno_status ws_ok = AKENO_OK;
            if (sm && akn_voice_flag_json("mute", "muted", m, msg, sizeof(msg)) > 0) ws_ok = send_text(&ws, msg);
            if (ws_ok == AKENO_OK && sd && akn_voice_flag_json("deafen", "deafened", d, msg, sizeof(msg)) > 0) ws_ok = send_text(&ws, msg);
            for (int i = 0; i < n && ws_ok == AKENO_OK; i++) ws_ok = send_frame(&ws, 2, out[i].data, out[i].len);
            if (ws_ok == AKENO_OK && now - last_ping >= VOICE_PING_MS) {
                last_ping = now;
                SDL_LockMutex(app->lock);
                akn_voice vs = app->voice;
                SDL_UnlockMutex(app->lock);
                akn_logf(&app->log, AKN_LOG_INFO, "voice.stats",
                         "sent=%lu received=%lu concealed=%lu late=%lu rebuffers=%lu rx_level=%d rx_tone=%d mic=%s mic_level=%d tx_tone=%d "
                         "muted=%d deafened=%d out=%d mic_volume=%d rtt=%d people=%d",
                         vs.sent, vs.received, vs.concealed, vs.late, vs.underruns, vs.rx_level, vs.rx_tone_hz, akn_mic_state_text(vs.mic),
                         vs.mic_level, vs.tx_tone_hz, vs.muted, vs.deafened, vs.out_volume, vs.mic_volume, vs.latency_ms, vs.people_count);
                if (akn_voice_ping_json((long long)now, msg, sizeof(msg)) > 0) ws_ok = send_text(&ws, msg);
            }
            SDL_LockMutex(app->lock);
            app->voice.sent += (unsigned long)n;
            SDL_UnlockMutex(app->lock);
            if (ws_ok != AKENO_OK) {
                snprintf(err, err_len, "The voice connection was lost.");
                close_code = 1006;
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
        last_rx = akn_platform_now_ms();
        if (f.opcode == 1) {
            int was_ready = ready;
            handle_text(rt, (const char *)f.payload, f.len, &ready, err, err_len);
            if (!was_ready && ready) {
                set_state(app, AKN_VOICE_CONNECTED, NULL);
                SDL_LockMutex(app->lock);
                app->voice.attempts = 0;
                SDL_UnlockMutex(app->lock);
                SDL_LockMutex(rt->lock);
                rt->connected = 1;
                akn_jitter_init(&rt->jitter);
                SDL_UnlockMutex(rt->lock);
                akn_logf(&app->log, AKN_LOG_INFO, "voice.connected", "In voice (gateway on the AKENO server's host)");
            }
        } else if (f.opcode == 2 && ready) {
            uint16_t seq;
            const uint8_t *opus;
            size_t opus_len;
            if (akn_voice_unpack_audio(f.payload, f.len, &seq, &opus, &opus_len) == 0) {
                SDL_LockMutex(rt->lock);
                akn_jitter_put(&rt->jitter, seq, opus, opus_len);
                SDL_UnlockMutex(rt->lock);
                SDL_LockMutex(app->lock);
                app->voice.received++;
                SDL_UnlockMutex(app->lock);
            }
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
    SDL_LockMutex(rt->lock);
    rt->connected = 0;
    rt->tx_count = 0;
    SDL_UnlockMutex(rt->lock);
    akeno_ws_free(&ws);
    akeno_conn_close(conn);
    if (!err[0] || close_code >= 4000) snprintf(err, err_len, "%s", akn_voice_close_text(close_code));
    akn_logf(&app->log, AKN_LOG_INFO, "voice.closed", "Voice connection closed (code %d)", close_code);
    return close_code;
}

static int net_thread(void *arg) {
    akn_voice_rt *rt = arg;
    akn_app *app = rt->app;
    char err[200], server[320], settings[300];
    akn_voice_credential *cred = malloc(sizeof(*cred));
    snprintf(settings, sizeof(settings), "%s/settings.json", app->platform.akeno_dir);
    while (cred && running(rt)) {
        set_state(app, AKN_VOICE_REQUESTING, NULL);
        int retry = 0, code = 0;
        err[0] = '\0';
        if (request_credential(rt, cred, err, sizeof(err), &retry) != 0) {
            code = retry ? 1006 : 1000;
        } else if (akn_read_server_setting(settings, server, sizeof(server)) != 0 || !akn_voice_same_host(cred->url, server)) {
            snprintf(err, sizeof(err), "The voice gateway is not on this console's AKENO server; AKENO Native does not connect to it.");
            code = 1000;
        } else {
            set_state(app, AKN_VOICE_CONNECTING, NULL);
            code = run_connection(rt, cred, err, sizeof(err));
        }
        memset(cred, 0, sizeof(*cred));
        if (!running(rt)) break;
        SDL_LockMutex(app->lock);
        int attempts = ++app->voice.attempts;
        if (code != 1000) app->voice.reconnects++;
        SDL_UnlockMutex(app->lock);
        if (code == 1000 || !akn_voice_should_retry(code, attempts - 1)) {
            set_state(app, AKN_VOICE_FAILED, err[0] ? err : "Voice ended.");
            rt->run = 0;
            break;
        }
        uint32_t pause = akn_voice_backoff_ms(attempts);
        SDL_LockMutex(app->lock);
        app->voice.retry_at = akn_platform_now_ms() + pause;
        SDL_UnlockMutex(app->lock);
        set_state(app, AKN_VOICE_RECONNECTING, err);
        sleep_while_running(rt, pause);
    }
    free(cred);
    rt->net_done = 1;
    return 0;
}

/* ---- Playback thread -------------------------------------------------------------------------- */

static int play_thread(void *arg) {
    akn_voice_rt *rt = arg;
    akn_app *app = rt->app;
    const akn_audio_ops *ops = akn_platform_audio_ops();
    int32_t rc = ops->init(ops->user);
    int32_t handle = rc == 0 || rc == AKN_AUDIO_ALREADY_INITIALIZED ? ops->open(ops->user, VOICE_OUT_GRAIN, AKN_VOICE_RATE, AUDIO_LAB_FORMAT_S16_STEREO) : rc;
    if (handle < 0) {
        akn_logf(&app->log, AKN_LOG_WARN, "voice.output", "The audio output could not be opened: 0x%08X", (unsigned)handle);
        SDL_LockMutex(app->lock);
        snprintf(app->voice.error, sizeof(app->voice.error), "The console's audio output could not be opened (0x%08X).", (unsigned)handle);
        SDL_UnlockMutex(app->lock);
        rt->play_done = 1;
        return 0;
    }
    akn_logf(&app->log, AKN_LOG_INFO, "voice.output", "Audio output open (%s, 48 kHz stereo, %d frames)", ops->backend, VOICE_OUT_GRAIN);
    akn_voice_decoder *dec = akn_voice_decoder_new(NULL);
    akn_pcm_fifo *fifo = malloc(sizeof(*fifo));
    int16_t *mono = malloc(sizeof(int16_t) * VOICE_OUT_GRAIN), *stereo = malloc(sizeof(int16_t) * 2 * VOICE_OUT_GRAIN);
    int16_t frame[AKN_VOICE_FRAME];
    uint8_t packet[AKN_VOICE_MAX_PACKET];
    int16_t window[4800];
    size_t win_fill = 0;
    if (!dec || !fifo || !mono || !stereo) goto done;
    akn_pcm_fifo_init(fifo);
    while (running(rt)) {
        while (fifo->count < VOICE_OUT_GRAIN) {
            size_t len = 0;
            SDL_LockMutex(rt->lock);
            akn_jitter_result r = AKN_JITTER_EMPTY;
            size_t got = 0;
            if (rt->test) got = akn_pcm_fifo_read(&rt->monitor, frame, AKN_VOICE_FRAME);
            else r = akn_jitter_get(&rt->jitter, packet, &len);
            unsigned long lost = rt->jitter.lost, under = rt->jitter.underruns, late = rt->jitter.late, over = rt->jitter.dropped_overflow;
            SDL_UnlockMutex(rt->lock);
            if (rt->test) {
                if (got < AKN_VOICE_FRAME) memset(frame + got, 0, sizeof(int16_t) * (AKN_VOICE_FRAME - got));
            } else if (r == AKN_JITTER_PACKET) {
                akn_voice_decode(dec, packet, len, frame);
            } else if (r == AKN_JITTER_LOST) {
                akn_voice_decode(dec, NULL, 0, frame);
            } else {
                memset(frame, 0, sizeof(frame));
            }
            akn_pcm_fifo_write(fifo, frame, AKN_VOICE_FRAME);
            SDL_LockMutex(app->lock);
            app->voice.concealed = lost;
            app->voice.underruns = under;
            app->voice.late = late;
            app->voice.overflow = over;
            SDL_UnlockMutex(app->lock);
        }
        akn_pcm_fifo_read(fifo, mono, VOICE_OUT_GRAIN);
        SDL_LockMutex(app->lock);
        int volume = app->voice.deafened ? 0 : app->voice.out_volume;
        SDL_UnlockMutex(app->lock);
        /* what is played: level and tone over 100 ms windows (diagnostics and the PC test) */
        for (int i = 0; i < VOICE_OUT_GRAIN; i++) {
            window[win_fill++] = volume ? mono[i] : 0;
            if (win_fill == sizeof(window) / sizeof(window[0])) {
                int level = akn_meter_percent(akn_pcm_rms_dbfs(window, win_fill));
                int hz = akn_pcm_tone_hz(window, win_fill, AKN_VOICE_RATE);
                SDL_LockMutex(app->lock);
                app->voice.rx_level = level;
                if (hz) app->voice.rx_tone_hz = hz;
                SDL_UnlockMutex(app->lock);
                win_fill = 0;
            }
        }
        akn_pcm_mono_to_stereo(mono, VOICE_OUT_GRAIN, volume, stereo);
        rc = ops->output(ops->user, handle, stereo);
        if (rc < 0) {
            akn_logf(&app->log, AKN_LOG_WARN, "voice.output", "Audio output failed: 0x%08X", (unsigned)rc);
            SDL_Delay(20);
        }
    }
done:
    ops->close(ops->user, handle);
    akn_voice_decoder_free(dec);
    free(fifo);
    free(mono);
    free(stereo);
    rt->play_done = 1;
    return 0;
}

/* ---- Capture thread --------------------------------------------------------------------------- */

static int cap_thread(void *arg) {
    akn_voice_rt *rt = arg;
    akn_app *app = rt->app;
    char why[160] = "";
    const akn_mic_ops *mic = akn_platform_mic(why, sizeof(why));
    if (!mic) {
        set_mic(app, AKN_MIC_UNAVAILABLE, 0, why);
        akn_logf(&app->log, AKN_LOG_WARN, "voice.mic", "No microphone: %s", why);
        rt->cap_done = 1;
        return 0;
    }
    unsigned rate = 0;
    int32_t handle = mic->open(VOICE_MIC_GRAIN, &rate);
    if (handle < 0) {
        set_mic(app, AKN_MIC_FAILED, handle, mic->backend);
        akn_logf(&app->log, AKN_LOG_WARN, "voice.mic", "The microphone could not be opened: 0x%08X (%s)", (unsigned)handle, mic->backend);
        rt->cap_done = 1;
        return 0;
    }
    set_mic(app, AKN_MIC_OPEN, 0, mic->backend);
    SDL_LockMutex(app->lock);
    app->voice.mic_rate = (int)rate;
    SDL_UnlockMutex(app->lock);
    akn_logf(&app->log, AKN_LOG_INFO, "voice.mic", "Microphone open (%s, %u Hz, %d frames per read)", mic->backend, rate, VOICE_MIC_GRAIN);

    akn_voice_encoder *enc = rt->test ? NULL : akn_voice_encoder_new(NULL);
    akn_upsample3 *up = malloc(sizeof(*up));
    akn_frame_acc *acc = malloc(sizeof(*acc));
    int16_t in[VOICE_MIC_GRAIN], wide[VOICE_MIC_GRAIN * 3];
    uint8_t opus[AKN_VOICE_MAX_PACKET];
    uint16_t seq = 0;
    int errors = 0, fast = 0;
    if ((!rt->test && !enc) || !up || !acc) goto done;
    akn_upsample3_init(up);
    akn_frame_acc_init(acc);
    while (running(rt)) {
        uint64_t t0 = akn_platform_now_us();
        int32_t rc = mic->read(handle, in);
        if (rc < 0) {
            errors++;
            if (errors == 1 || errors % 50 == 0)
                akn_logf(&app->log, AKN_LOG_WARN, "voice.mic", "Microphone read failed: 0x%08X (%d times)", (unsigned)rc, errors);
            set_mic(app, errors >= 50 ? AKN_MIC_FAILED : AKN_MIC_OPEN, rc, NULL);
            SDL_Delay(16);
            continue;
        }
        errors = 0;
        /* a read that returns at once every time would spin: pace it to the grain */
        if (akn_platform_now_us() - t0 < 2000) {
            if (++fast >= 20) SDL_Delay(VOICE_MIC_GRAIN * 1000 / (rate ? rate : 16000));
        } else {
            fast = 0;
        }
        const int16_t *pcm = in;
        size_t n = VOICE_MIC_GRAIN;
        if (rate == 16000) {
            akn_upsample3_run(up, in, VOICE_MIC_GRAIN, wide);
            pcm = wide;
            n = VOICE_MIC_GRAIN * 3;
        }
        size_t used = 0;
        while (used < n) {
            int ready = 0;
            used += akn_frame_acc_add(acc, pcm + used, n - used, &ready);
            if (!ready) continue;
            SDL_LockMutex(app->lock);
            int gain = app->voice.mic_volume, muted = app->voice.muted;
            SDL_UnlockMutex(app->lock);
            akn_pcm_gain(acc->buf, AKN_VOICE_FRAME, gain);
            int level = akn_meter_percent(akn_pcm_rms_dbfs(acc->buf, AKN_VOICE_FRAME));
            int peak = akn_pcm_peak(acc->buf, AKN_VOICE_FRAME);
            int hz = akn_pcm_tone_hz(acc->buf, AKN_VOICE_FRAME, AKN_VOICE_RATE);
            SDL_LockMutex(app->lock);
            app->voice.mic_level = level;
            if (peak > app->voice.mic_peak) app->voice.mic_peak = peak;
            if (hz) app->voice.tx_tone_hz = hz;
            app->voice.mic_frames++;
            int monitor = app->voice.mic_monitor;
            SDL_UnlockMutex(app->lock);
            if (rt->test) {
                if (monitor) {
                    SDL_LockMutex(rt->lock);
                    akn_pcm_fifo_write(&rt->monitor, acc->buf, AKN_VOICE_FRAME);
                    SDL_UnlockMutex(rt->lock);
                }
            } else if (!muted) {
                SDL_LockMutex(rt->lock);
                int connected = rt->connected;
                SDL_UnlockMutex(rt->lock);
                int len = connected ? akn_voice_encode(enc, acc->buf, opus, sizeof(opus)) : -1;
                if (len > 0) {
                    SDL_LockMutex(rt->lock);
                    if (rt->tx_count == VOICE_TX_QUEUE) { /* the network is behind: the oldest goes */
                        rt->tx_head = (rt->tx_head + 1) % VOICE_TX_QUEUE;
                        rt->tx_count--;
                    }
                    tx_packet *p = &rt->tx[(rt->tx_head + rt->tx_count) % VOICE_TX_QUEUE];
                    int packed = akn_voice_pack_audio(seq++, opus, (size_t)len, p->data, sizeof(p->data));
                    if (packed > 0) {
                        p->len = (size_t)packed;
                        rt->tx_count++;
                    }
                    SDL_UnlockMutex(rt->lock);
                }
            }
            akn_frame_acc_init(acc);
        }
    }
done:
    mic->close(handle);
    set_mic(app, AKN_MIC_CLOSED, 0, NULL);
    SDL_LockMutex(app->lock);
    app->voice.mic_level = 0;
    SDL_UnlockMutex(app->lock);
    akn_logf(&app->log, AKN_LOG_INFO, "voice.mic", "Microphone closed");
    akn_voice_encoder_free(enc);
    free(up);
    free(acc);
    rt->cap_done = 1;
    return 0;
}

/* ---- Control (main thread) -------------------------------------------------------------------- */

static akn_voice_rt *start(akn_app *app, int test, const char *party_id) {
    akn_voice_rt *rt = calloc(1, sizeof(*rt));
    if (!rt) return NULL;
    rt->app = app;
    rt->lock = SDL_CreateMutex();
    rt->run = 1;
    rt->test = test;
    snprintf(rt->party_id, sizeof(rt->party_id), "%s", party_id ? party_id : "");
    akn_jitter_init(&rt->jitter);
    akn_pcm_fifo_init(&rt->monitor);
    rt->net_done = test;
    /* libopus keeps its work buffers on the stack (VAR_ARRAYS) and TLS needs room too: explicit
     * stacks, so the console's default thread stack size does not matter */
    rt->play = SDL_CreateThreadWithStackSize(play_thread, "akn-voice-out", VOICE_THREAD_STACK, rt);
    rt->cap = SDL_CreateThreadWithStackSize(cap_thread, "akn-voice-mic", VOICE_THREAD_STACK, rt);
    if (!test) rt->net = SDL_CreateThreadWithStackSize(net_thread, "akn-voice-net", VOICE_THREAD_STACK, rt);
    if (!rt->play) rt->play_done = 1;
    if (!rt->cap) rt->cap_done = 1;
    if (!test && !rt->net) rt->net_done = 1;
    return rt;
}

int akn_voice_busy(akn_app *app) {
    (void)app;
    return g_rt != NULL;
}

int akn_voice_join(akn_app *app, const char *party_id, const char *party_name, char *why, size_t why_len) {
    if (g_rt) {
        snprintf(why, why_len, "%s", g_rt->test ? "Stop the microphone test first." : "Voice is still closing. Try again in a moment.");
        return -1;
    }
    SDL_LockMutex(app->lock);
    int ipc = app->social.daemon.ipc, audio = app->audio_running;
    SDL_UnlockMutex(app->lock);
    if (ipc < 3) {
        snprintf(why, why_len, "Voice needs the native-IPC daemon of this package (native interface 3).");
        return -1;
    }
    if (audio) {
        snprintf(why, why_len, "Wait until the audio test has finished.");
        return -1;
    }
    SDL_LockMutex(app->lock);
    int out = app->voice.out_volume, micv = app->voice.mic_volume;
    akn_voice_init(&app->voice);
    app->voice.out_volume = out ? out : 100;
    app->voice.mic_volume = micv ? micv : 100;
    snprintf(app->voice.party_id, sizeof(app->voice.party_id), "%s", party_id);
    snprintf(app->voice.party_name, sizeof(app->voice.party_name), "%s", party_name ? party_name : "");
    app->voice.state = AKN_VOICE_REQUESTING;
    SDL_UnlockMutex(app->lock);
    g_rt = start(app, 0, party_id);
    if (!g_rt) {
        snprintf(why, why_len, "Out of memory.");
        return -1;
    }
    akn_logf(&app->log, AKN_LOG_INFO, "voice.join", "Joining party voice (the user confirmed the microphone)");
    return 0;
}

int akn_voice_mic_test(akn_app *app, char *why, size_t why_len) {
    if (g_rt) {
        snprintf(why, why_len, "%s", g_rt->test ? "The microphone test is already running." : "Leave voice first.");
        return -1;
    }
    SDL_LockMutex(app->lock);
    int audio = app->audio_running;
    int out = app->voice.out_volume, micv = app->voice.mic_volume;
    SDL_UnlockMutex(app->lock);
    if (audio) {
        snprintf(why, why_len, "Wait until the audio test has finished.");
        return -1;
    }
    SDL_LockMutex(app->lock);
    akn_voice_init(&app->voice);
    app->voice.out_volume = out ? out : 100;
    app->voice.mic_volume = micv ? micv : 100;
    app->voice.mic_test = 1;
    SDL_UnlockMutex(app->lock);
    g_rt = start(app, 1, NULL);
    if (!g_rt) {
        snprintf(why, why_len, "Out of memory.");
        return -1;
    }
    akn_logf(&app->log, AKN_LOG_INFO, "voice.mic_test", "Microphone test started (local only; nothing is sent or stored)");
    return 0;
}

void akn_voice_leave(akn_app *app, const char *reason) {
    if (!g_rt) return;
    g_rt->run = 0;
    SDL_LockMutex(app->lock);
    int test = app->voice.mic_test;
    app->voice.mic_test = 0;
    app->voice.mic_monitor = 0;
    if (app->voice.state != AKN_VOICE_FAILED) {
        app->voice.state = AKN_VOICE_OFF;
        snprintf(app->voice.error, sizeof(app->voice.error), "%s", reason ? reason : "");
    }
    app->voice.people_count = 0;
    SDL_UnlockMutex(app->lock);
    akn_logf(&app->log, AKN_LOG_INFO, test ? "voice.mic_test" : "voice.leave", "%s", reason ? reason : "Stopped");
}

void akn_voice_toggle_mute(akn_app *app) {
    SDL_LockMutex(app->lock);
    app->voice.muted = !app->voice.muted;
    if (!app->voice.muted) app->voice.deafened = 0; /* unmuting also undeafens */
    SDL_UnlockMutex(app->lock);
    if (g_rt) {
        SDL_LockMutex(g_rt->lock);
        g_rt->send_mute = g_rt->send_deafen = 1;
        SDL_UnlockMutex(g_rt->lock);
    }
}

void akn_voice_toggle_deafen(akn_app *app) {
    SDL_LockMutex(app->lock);
    app->voice.deafened = !app->voice.deafened;
    if (app->voice.deafened) app->voice.muted = 1; /* deafened also means not speaking */
    SDL_UnlockMutex(app->lock);
    if (g_rt) {
        SDL_LockMutex(g_rt->lock);
        g_rt->send_mute = g_rt->send_deafen = 1;
        SDL_UnlockMutex(g_rt->lock);
    }
}

void akn_voice_adjust(akn_app *app, int which, int dir) {
    SDL_LockMutex(app->lock);
    int *v = which ? &app->voice.mic_volume : &app->voice.out_volume;
    *v = akn_voice_volume_step(*v, dir);
    SDL_UnlockMutex(app->lock);
}

void akn_voice_toggle_monitor(akn_app *app) {
    SDL_LockMutex(app->lock);
    if (app->voice.mic_test) app->voice.mic_monitor = !app->voice.mic_monitor;
    SDL_UnlockMutex(app->lock);
}

static void free_rt(akn_voice_rt *rt) {
    if (rt->net) SDL_WaitThread(rt->net, NULL);
    if (rt->play) SDL_WaitThread(rt->play, NULL);
    if (rt->cap) SDL_WaitThread(rt->cap, NULL);
    SDL_DestroyMutex(rt->lock);
    free(rt);
}

void akn_voice_poll(akn_app *app) {
    if (!g_rt) return;
    if (g_rt->run) {
        /* the network thread ended the session (removed, failed): stop the audio threads too */
        if (g_rt->net_done && !g_rt->test) g_rt->run = 0;
        else return;
    }
    if (!(g_rt->net_done && g_rt->play_done && g_rt->cap_done)) return;
    free_rt(g_rt);
    g_rt = NULL;
    SDL_LockMutex(app->lock);
    app->voice.mic_test = 0;
    if (app->voice.state != AKN_VOICE_FAILED) app->voice.state = AKN_VOICE_OFF;
    SDL_UnlockMutex(app->lock);
}

void akn_voice_shutdown(akn_app *app) {
    if (!g_rt) return;
    akn_voice_leave(app, "AKENO Native closed");
    uint64_t end = akn_platform_now_ms() + 12000; /* a connect can take up to its timeout */
    while (!(g_rt->net_done && g_rt->play_done && g_rt->cap_done) && akn_platform_now_ms() < end) SDL_Delay(10);
    if (g_rt->net_done && g_rt->play_done && g_rt->cap_done) {
        free_rt(g_rt);
        g_rt = NULL;
    }
}
