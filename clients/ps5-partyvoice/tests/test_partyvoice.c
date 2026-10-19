/*
 * Unit tests of AKENO Party Voice on a PC (simulated console, fake daemon). The real server,
 * LiveKit and gateway are exercised by tests/e2e/run.mjs. Nothing here is evidence about a PS5.
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "akeno/json.h"
#include "buf.h"
#include "lab_pcm.h"
#include "pv.h"
#include "pv_backend.h"
#include "pv_control.h"
#include "pv_engine.h"
#include "pv_internal.h"
#include "pv_status.h"

static int g_checks, g_failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_failures++;                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                          \
    } while (0)

static int contains(const char *hay, const char *needle) { return hay && strstr(hay, needle) != NULL; }

static const char KEY[] = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

/* ---- A fake daemon: /api/instance and the voice pass, answers chosen by the test ---------------- */

typedef struct {
    int fd, port;
    pthread_mutex_t lock;
    char instance[256];
    char pass[1024];
    int pass_requests;
    char last_key[100];
} fake_daemon;

static void *fake_main(void *arg) {
    fake_daemon *f = arg;
    for (;;) {
        int c = accept(f->fd, NULL, NULL);
        if (c < 0) return NULL;
        char buf[8192];
        size_t got = 0;
        while (got < sizeof(buf) - 1) {
            ssize_t n = read(c, buf + got, sizeof(buf) - 1 - got);
            if (n <= 0) break;
            got += (size_t)n;
            buf[got] = '\0';
            char *body = strstr(buf, "\r\n\r\n");
            if (!body) continue;
            const char *cl = strstr(buf, "Content-Length: ");
            if ((long)(got - (size_t)(body + 4 - buf)) >= (cl ? atol(cl + 16) : 0)) break;
        }
        buf[got] = '\0';
        char method[8] = "", path[200] = "";
        sscanf(buf, "%7s %199s", method, path);
        pthread_mutex_lock(&f->lock);
        const char *answer = "{\"ok\":false,\"status\":404,\"error\":{\"code\":\"NOT_FOUND\",\"message\":\"no\"}}";
        if (strcmp(path, "/api/instance") == 0) answer = f->instance;
        else if (strstr(path, "/voice/device-token")) {
            f->pass_requests++;
            answer = f->pass;
            const char *k = strstr(buf, "X-Akeno-Native-Key: ");
            if (k) sscanf(k + 20, "%99s", f->last_key);
        }
        char head[200];
        int n = snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                         strlen(answer));
        if (write(c, head, (size_t)n) < 0 || write(c, answer, strlen(answer)) < 0) {
            /* the client went away */
        }
        pthread_mutex_unlock(&f->lock);
        close(c);
    }
}

static int fake_start(fake_daemon *f, pthread_t *thread) {
    memset(f, 0, sizeof(*f));
    pthread_mutex_init(&f->lock, NULL);
    snprintf(f->instance, sizeof(f->instance), "{\"app\":\"akeno-network-console\",\"version\":\"0.5.0-alpha\",\"nativeIpc\":3}");
    f->fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(0x7F000001u);
    socklen_t len = sizeof(a);
    if (f->fd < 0 || bind(f->fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(f->fd, 16) != 0 || getsockname(f->fd, (struct sockaddr *)&a, &len) != 0)
        return -1;
    f->port = ntohs(a.sin_port);
    return pthread_create(thread, NULL, fake_main, f);
}

static void fake_set(fake_daemon *f, const char *instance, const char *pass) {
    pthread_mutex_lock(&f->lock);
    if (instance) snprintf(f->instance, sizeof(f->instance), "%s", instance);
    if (pass) snprintf(f->pass, sizeof(f->pass), "%s", pass);
    f->pass_requests = 0;
    pthread_mutex_unlock(&f->lock);
}

static int fake_requests(fake_daemon *f) {
    pthread_mutex_lock(&f->lock);
    int n = f->pass_requests;
    pthread_mutex_unlock(&f->lock);
    return n;
}

/* ---- Helpers ---------------------------------------------------------------------------------- */

typedef struct {
    char root[64], akeno[128], data[128];
    pv_sim sim;
    pv_backend be;
    pv_config cfg;
    pv_engine *e;
} world;

static void write_text(const char *path, const char *text, mode_t mode) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(text, f);
    fclose(f);
    chmod(path, mode);
}

static void world_open(world *w, int daemon_port) {
    memset(w, 0, sizeof(*w));
    snprintf(w->root, sizeof(w->root), "/tmp/akeno-pv-test-XXXXXX");
    if (!mkdtemp(w->root)) return;
    snprintf(w->akeno, sizeof(w->akeno), "%s/akeno", w->root);
    snprintf(w->data, sizeof(w->data), "%s/pv", w->root);
    mkdir(w->akeno, 0700);
    mkdir(w->data, 0755);
    char path[200];
    snprintf(path, sizeof(path), "%s/native.key", w->akeno);
    write_text(path, KEY, 0600);
    snprintf(path, sizeof(path), "%s/settings.json", w->akeno);
    write_text(path, "{\"server\":\"https://127.0.0.1:1\"}", 0600);
    pv_sim_from_env(&w->sim);
    pv_backend_host(&w->be, &w->sim);
    w->cfg.be = &w->be;
    snprintf(w->cfg.akeno_dir, sizeof(w->cfg.akeno_dir), "%s", w->akeno);
    snprintf(w->cfg.data_dir, sizeof(w->cfg.data_dir), "%s", w->data);
    w->cfg.daemon_port = daemon_port;
    w->cfg.notifications = 1;
    w->e = pv_engine_new(&w->cfg);
}

static void world_close(world *w) {
    pv_engine_free(w->e);
    char cmd[200];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", w->root);
    if (system(cmd) != 0) fprintf(stderr, "cleanup of %s failed\n", w->root);
}

static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* Ticks the engine like the main loop until cond holds or ms pass (slow CI machines). */
#define WAIT_UNTIL(e, cond, ms)                                   \
    for (int wait_t_ = 0; wait_t_ < (ms) && !(cond); wait_t_ += 20) { \
        pv_engine_tick(e);                                        \
        sleep_ms(20);                                             \
    }

typedef int (*status_pred)(const pv_status *s);

/* Ticks the engine like the main loop until pred holds; 1 when it did within ms. */
static int wait_for(pv_engine *e, status_pred pred, int ms) {
    pv_status s;
    for (int t = 0; t < ms; t += 20) {
        pv_engine_tick(e);
        pv_engine_status(e, &s);
        if (pred(&s)) return 1;
        sleep_ms(20);
    }
    return 0;
}

/* like wait_for, without ticking: what the plugin reads between two ticks */
static int wait_net_untick(pv_engine *e, pv_net_state state, pv_status *s, int ms) {
    for (int t = 0; t < ms; t += 5) {
        pv_engine_status(e, s);
        if (s->net == state) return 1;
        sleep_ms(5);
    }
    return 0;
}

static int is_ended(const pv_status *s) { return s->net == PV_NET_ENDED && !s->in_voice; }
static int is_reconnecting(const pv_status *s) { return s->net == PV_NET_RECONNECTING; }
static int mic_active(const pv_status *s) { return s->mic == PV_MIC_ACTIVE && s->mic_frames > 10; }
static int mic_off(const pv_status *s) { return s->mic == PV_MIC_OFF && s->mic_level == 0; }
static int mic_failed(const pv_status *s) { return s->mic == PV_MIC_FAILED || s->mic == PV_MIC_UNAVAILABLE; }
static int out_failed(const pv_status *s) { return s->out == PV_OUT_FAILED && s->silent_frames > 20; }
static int test_finished(const pv_status *s) { return s->test_state != PV_TEST_NONE && s->test_state != PV_TEST_PLAYING; }
static int out_running(const pv_status *s) { return s->out == PV_OUT_ACTIVE && s->out_calls > 12; }

/* The test sound's own thread has ended and been joined (its port is closed). */
static int test_idle(pv_engine *e) {
    pv_engine_tick(e);
    pthread_mutex_lock(&e->lock);
    int idle = !e->have_test && !e->test_pending;
    pthread_mutex_unlock(&e->lock);
    return idle;
}

static void request(char *buf, size_t len, const char *method, const char *path, const char *key, const char *extra, const char *body) {
    int n = snprintf(buf, len, "%s %s HTTP/1.1\r\nHost: 127.0.0.1:8646\r\n", method, path);
    if (key) n += snprintf(buf + n, len - (size_t)n, "X-Akeno-Native-Key: %s\r\n", key);
    if (extra) n += snprintf(buf + n, len - (size_t)n, "%s", extra);
    if (body) n += snprintf(buf + n, len - (size_t)n, "Content-Type: application/json\r\nContent-Length: %zu\r\n", strlen(body));
    snprintf(buf + n, len - (size_t)n, "\r\n%s", body ? body : "");
}

static pv_response g_r;
static void call(world *w, const char *method, const char *path, const char *key, const char *extra, const char *body) {
    char buf[4096];
    pv_control_config c;
    memset(&c, 0, sizeof(c));
    snprintf(c.akeno_dir, sizeof(c.akeno_dir), "%s", w->akeno);
    c.port = 8646;
    request(buf, sizeof(buf), method, path, key, extra, body);
    pv_control_handle(w->e, &c, buf, strlen(buf), &g_r);
}

static const char *json_code(const char *body) {
    static char code[64];
    akeno_json_doc *doc = akeno_json_parse(body, strlen(body), NULL, 0);
    snprintf(code, sizeof(code), "%s", akeno_json_get_string(akeno_json_path(akeno_json_root(doc), "error"), "code", ""));
    akeno_json_free(doc);
    return code;
}

/* ---- Tests ------------------------------------------------------------------------------------ */

static void test_basics(void) {
    CHECK(pv_route_get(0) == NULL && pv_route_get(5) == NULL);
    CHECK(pv_route_get(1) && pv_route_get(1)->foreground_user == 0 && pv_route_get(1)->port_type == 0);
    CHECK(pv_route_get(2) && pv_route_get(2)->foreground_user == 1 && pv_route_get(2)->port_type == 0 && !pv_route_get(2)->retired);
    /* the PS4 port numbers are retired: refused on 12.20 like route 2, meaning on a PS5 unknown */
    CHECK(pv_route_get(3) && pv_route_get(3)->retired && pv_route_get(4) && pv_route_get(4)->retired && !pv_route_get(1)->retired);
    CHECK(pv_party_id_ok("00000000-0000-4000-8000-000000000001"));
    CHECK(!pv_party_id_ok("") && !pv_party_id_ok("../x") && !pv_party_id_ok("a b") && !pv_party_id_ok("a/b"));
    char long_id[80];
    memset(long_id, 'a', 65);
    long_id[65] = '\0';
    CHECK(!pv_party_id_ok(long_id));
}

static void test_summary_and_evidence(void) {
    pv_status s;
    memset(&s, 0, sizeof(s));
    char t[160];
    pv_status_summary(&s, t, sizeof(t));
    CHECK(strcmp(t, "Not in voice") == 0);
    s.net = PV_NET_CONNECTING;
    pv_status_summary(&s, t, sizeof(t));
    CHECK(contains(t, "Connecting") && contains(t, "Microphone off"));
    s.net = PV_NET_CONNECTED;
    s.out = PV_OUT_ACTIVE;
    s.mic = PV_MIC_ACTIVE;
    pv_status_summary(&s, t, sizeof(t));
    CHECK(strcmp(t, "Connected \xC2\xB7 Listening \xC2\xB7 Microphone active") == 0);
    s.muted = 1;
    pv_status_summary(&s, t, sizeof(t));
    CHECK(contains(t, "Microphone muted"));
    s.out = PV_OUT_FAILED;
    s.mic = PV_MIC_FAILED;
    pv_status_summary(&s, t, sizeof(t));
    CHECK(contains(t, "Audio output unavailable") && contains(t, "Microphone unavailable"));
    s.net = PV_NET_RECONNECTING;
    pv_status_summary(&s, t, sizeof(t));
    CHECK(contains(t, "Reconnecting"));
    s.net = PV_NET_ENDED;
    pv_status_summary(&s, t, sizeof(t));
    CHECK(strcmp(t, "Disconnected") == 0);
    /* evidence needs real counts, never just a state */
    memset(&s, 0, sizeof(s));
    s.net = PV_NET_CONNECTED;
    s.mic = PV_MIC_ACTIVE;
    s.out = PV_OUT_ACTIVE;
    s.out_calls = 1000;
    s.encoded = 100;
    s.sent = 100; /* all of them from silent frames */
    s.mic_open_calls = 3;
    CHECK(!pv_evidence_network(&s) && !pv_evidence_mic(&s) && !pv_evidence_speaker(&s) && !pv_evidence_remote_audio(&s));
    CHECK(!pv_evidence_submitted(&s) && !pv_evidence_mic_open(&s) && !pv_evidence_mic_capturing(&s));
    s.ever_connected = 1;
    s.sent_nonzero = 3;
    s.decoded_nonzero = 10;
    CHECK(pv_evidence_network(&s) && pv_evidence_mic(&s) && !pv_evidence_speaker(&s));
    CHECK(!pv_evidence_remote_audio(&s)); /* decoded sound needs received packets */
    s.received = 12;
    CHECK(pv_evidence_remote_audio(&s));
    s.out_nonzero_blocks = 5; /* blocks with sound, but from the test sound only */
    CHECK(!pv_evidence_speaker(&s) && !pv_evidence_submitted(&s));
    s.test_tone_blocks = 5;
    CHECK(pv_evidence_submitted(&s) && !pv_evidence_speaker(&s));
    s.out_speech_blocks = 5;
    CHECK(pv_evidence_speaker(&s));
    s.mic_open_ok = 1;
    CHECK(pv_evidence_mic_open(&s) && !pv_evidence_mic_capturing(&s));
    s.mic_nonzero_frames = 1;
    CHECK(pv_evidence_mic_capturing(&s));
}

static void fill_status(pv_status *s) {
    memset(s, 0, sizeof(*s));
    s->in_voice = 1;
    snprintf(s->party_id, sizeof(s->party_id), "11111111-2222-4333-8444-555555555555");
    s->net = PV_NET_CONNECTED;
    s->ever_connected = 1;
    s->ready_seen = s->participants_seen = 1;
    s->out = PV_OUT_ACTIVE;
    s->mic = PV_MIC_ACTIVE;
    s->mic_enabled = 1;
    s->route = 2;
    s->out_volume = 90;
    s->mic_gain = 110;
    s->paced = 1;
    s->people_count = 2;
    snprintf(s->people[0].id, sizeof(s->people[0].id), "u-self-0001");
    snprintf(s->people[0].name, sizeof(s->people[0].name), "Akeno \"Owner\"");
    s->people[0].self = 1;
    snprintf(s->people[1].id, sizeof(s->people[1].id), "u-bob-0002");
    snprintf(s->people[1].name, sizeof(s->people[1].name), "Bob Secretname");
    s->people[1].speaking = 1;
    s->sent = 500;
    s->sent_nonzero = 480;
    s->received = 600;
    s->decoded = 600;
    s->decoded_nonzero = 550;
    s->out_calls = 560;
    s->out_nonzero_blocks = 500;
    s->out_speech_blocks = 480;
    s->mic_open_ok = 1;
    s->mic_rate = 16000;
    s->probe_verdict = 1;
    s->probe_blocks = 12;
    s->probe_ms = 230;
    s->process_kind = AKN_CTX_BACKGROUND;
    snprintf(s->process_detail, sizeof(s->process_detail), "background process (own app 0x00000000 (no title ID), foreground app 0x12345678)");
    s->mic_frames = 500;
    s->mic_nonzero_frames = 498;
    s->gateway_self_level_max = 64;
    snprintf(s->platform, sizeof(s->platform), "PC (test)");
    snprintf(s->backend, sizeof(s->backend), "PC (test)");
    snprintf(s->mic_detail, sizeof(s->mic_detail), "simulated");
}

static void test_status_json(void) {
    pv_status s;
    fill_status(&s);
    akeno_buf b;
    akeno_buf_init(&b, 16384);
    CHECK(pv_status_json(&s, &b) == 0);
    akeno_json_doc *doc = akeno_json_parse(b.data, b.len, NULL, 0);
    const akeno_json *root = akeno_json_root(doc);
    CHECK(doc != NULL);
    CHECK(strcmp(akeno_json_get_string(root, "app", ""), PV_APP) == 0);
    CHECK(strcmp(akeno_json_path(root, "network.state") ? akeno_json_path(root, "network.state")->string : "", "connected") == 0);
    CHECK(akeno_json_get_bool(akeno_json_get(root, "evidence"), "micTransmitting", 0) == 1);
    CHECK(akeno_json_get_bool(akeno_json_get(root, "evidence"), "speakerOutput", 0) == 1);
    const akeno_json *ev = akeno_json_get(root, "evidence");
    CHECK(akeno_json_get_bool(ev, "remoteAudioReceived", 0) == 1 && akeno_json_get_bool(ev, "audioSubmitted", 0) == 1);
    CHECK(akeno_json_get_bool(ev, "micOpen", 0) == 1 && akeno_json_get_bool(ev, "micCapturing", 0) == 1);
    /* audible and heard are never a measurement */
    CHECK(strcmp(akeno_json_get_string(ev, "audioAudible", ""), "unconfirmed") == 0);
    CHECK(strcmp(akeno_json_get_string(ev, "remoteHeardMic", ""), "unconfirmed") == 0);
    CHECK(strcmp(akeno_json_get_string(akeno_json_get(root, "process"), "kind", ""), "background") == 0);
    CHECK(strcmp(akeno_json_get_string(akeno_json_get(root, "output"), "probe", ""), "consumed") == 0);
    CHECK(strcmp(akeno_json_get_string(akeno_json_get(root, "test"), "state", ""), "none") == 0);
    /* what plugin 1.20 reads stays where it was (protocol 1) */
    CHECK(akeno_json_int64(akeno_json_get(root, "protocol"), &(int64_t){ 0 }) == 0);
    CHECK(akeno_json_get_string(akeno_json_get(root, "mic"), "state", NULL) && akeno_json_get_string(akeno_json_get(root, "output"), "paced", NULL));
    const akeno_json *people = akeno_json_get(root, "participants");
    CHECK(people && people->count == 2);
    CHECK(strcmp(akeno_json_get_string(akeno_json_at(people, 0), "name", ""), "Akeno \"Owner\"") == 0);
    CHECK(akeno_json_get_bool(akeno_json_at(people, 1), "speaking", 0) == 1);
    int64_t v = 0;
    CHECK(akeno_json_int64(akeno_json_path(root, "output.route"), &v) == 0 && v == 2);
    CHECK(strcmp(akeno_json_path(root, "output.paced")->string, "yes") == 0);
    akeno_json_free(doc);
    akeno_buf_free(&b);
}

static void test_report_privacy(void) {
    pv_status s;
    fill_status(&s);
    static char text[24 * 1024];
    size_t n = pv_report_text(&s, text, sizeof(text));
    CHECK(n > 1000 && n < sizeof(text) - 1);
    for (const char *section = "[Summary\0[Diagnosis]\0[Network]\0[Microphone]\0[Speakers]\0[Test sound\0[Runtime]\0[Your answers\0"; *section;
         section += strlen(section) + 1)
        CHECK(contains(text, section));
    /* the eight outcomes, each on its own line */
    CHECK(contains(text, "NETWORK CONNECTED:             yes"));
    CHECK(contains(text, "REMOTE AUDIO RECEIVED:         yes - 600 packets received"));
    CHECK(contains(text, "AUDIO SUBMITTED:               yes - accepted output blocks with sound: 480 of party speech"));
    CHECK(contains(text, "AUDIO AUDIBLE:                 NOT CONFIRMED - only you can answer it"));
    CHECK(contains(text, "MICROPHONE OPEN:               yes"));
    CHECK(contains(text, "MICROPHONE CAPTURING:          yes"));
    CHECK(contains(text, "MICROPHONE TRANSMITTING:       yes"));
    CHECK(contains(text, "REMOTE USER HEARD MICROPHONE:  NOT CONFIRMED - only your friend can answer it"));
    CHECK(!contains(text, "AUDIO AUDIBLE:                 yes") && !contains(text, "HEARD MICROPHONE:  yes"));
    CHECK(contains(text, "consumed by an audio clock"));
    CHECK(contains(text, "Process:   background process"));
    CHECK(contains(text, "the background-process limit")); /* background + submitted: the diagnosis says what it means */
    CHECK(contains(text, "I hear my friend"));
    CHECK(contains(text, "my friend hears me"));
    /* no names, no IDs */
    CHECK(!contains(text, "Bob") && !contains(text, "Secretname") && !contains(text, "Owner"));
    CHECK(!contains(text, "u-bob") && !contains(text, "u-self") && !contains(text, "11111111-2222"));
    /* a small buffer is cut, never overrun */
    char small[64];
    CHECK(pv_report_text(&s, small, sizeof(small)) < sizeof(small));
}

static void test_control_security(void) {
    world w;
    world_open(&w, 1);
    call(&w, "GET", "/api/voice/hello", NULL, NULL, NULL);
    CHECK(g_r.status == 200 && contains(g_r.body, "\"protocol\":1"));
    call(&w, "GET", "/api/voice/status", NULL, NULL, NULL);
    CHECK(g_r.status == 403 && strcmp(json_code(g_r.body), "NATIVE_KEY_INVALID") == 0);
    call(&w, "GET", "/api/voice/status", "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff", NULL, NULL);
    CHECK(g_r.status == 403);
    call(&w, "GET", "/api/voice/status", KEY, "Origin: http://127.0.0.1:8646\r\n", NULL);
    CHECK(g_r.status == 403 && strcmp(json_code(g_r.body), "ORIGIN_REFUSED") == 0);
    call(&w, "GET", "/api/voice/status", KEY, NULL, NULL);
    CHECK(g_r.status == 200 && contains(g_r.body, "\"summary\":\"Not in voice\""));
    /* a key file that is not private is not a key */
    char path[200];
    snprintf(path, sizeof(path), "%s/native.key", w.akeno);
    chmod(path, 0644);
    call(&w, "GET", "/api/voice/status", KEY, NULL, NULL);
    CHECK(g_r.status == 403);
    chmod(path, 0600);
    /* Host must be this interface's (DNS rebinding) */
    pv_control_config c;
    memset(&c, 0, sizeof(c));
    snprintf(c.akeno_dir, sizeof(c.akeno_dir), "%s", w.akeno);
    c.port = 8646;
    const char *evil = "GET /api/voice/status HTTP/1.1\r\nHost: evil.example\r\nX-Akeno-Native-Key: 0123\r\n\r\n";
    pv_control_handle(w.e, &c, evil, strlen(evil), &g_r);
    CHECK(g_r.status == 400 && strcmp(json_code(g_r.body), "BAD_HOST") == 0);
    pv_control_handle(w.e, &c, "garbage\r\n\r\n", 11, &g_r);
    CHECK(g_r.status == 400);
    /* strict bodies */
    call(&w, "POST", "/api/voice/join", KEY, NULL, "{\"partyId\":\"../../etc\"}");
    CHECK(g_r.status == 400 && strcmp(json_code(g_r.body), "INVALID_PARTY") == 0);
    call(&w, "POST", "/api/voice/join", KEY, NULL, "{\"partyId\":\"p-1\",\"mic\":true}");
    CHECK(g_r.status == 400 && strcmp(json_code(g_r.body), "INVALID_BODY") == 0);
    call(&w, "POST", "/api/voice/join", KEY, NULL, "{\"partyId\":5}");
    CHECK(g_r.status == 400);
    call(&w, "POST", "/api/voice/join", KEY, NULL, "[]");
    CHECK(g_r.status == 400);
    call(&w, "POST", "/api/voice/volume", KEY, NULL, "{\"output\":151}");
    CHECK(g_r.status == 400 && strcmp(json_code(g_r.body), "OUT_OF_RANGE") == 0);
    call(&w, "POST", "/api/voice/volume", KEY, NULL, "{\"mic\":49}");
    CHECK(g_r.status == 400);
    call(&w, "POST", "/api/voice/volume", KEY, NULL, "{\"output\":1.5}");
    CHECK(g_r.status == 400);
    call(&w, "POST", "/api/voice/volume", KEY, NULL, "{}");
    CHECK(g_r.status == 400);
    call(&w, "POST", "/api/voice/volume", KEY, NULL, "{\"output\":120,\"mic\":80}");
    CHECK(g_r.status == 200 && contains(g_r.body, "\"volume\":120") && contains(g_r.body, "\"gain\":80"));
    call(&w, "POST", "/api/voice/route", KEY, NULL, "{\"route\":5}");
    CHECK(g_r.status == 400);
    call(&w, "POST", "/api/voice/route", KEY, NULL, "{\"route\":3}");
    CHECK(g_r.status == 409 && strcmp(json_code(g_r.body), "ROUTE_RETIRED") == 0 && contains(g_r.body, "Use route 1"));
    call(&w, "POST", "/api/voice/route", KEY, NULL, "{\"route\":2}");
    CHECK(g_r.status == 200 && contains(g_r.body, "\"route\":2"));
    call(&w, "POST", "/api/voice/mute", KEY, NULL, "{\"muted\":true}");
    CHECK(g_r.status == 409 && strcmp(json_code(g_r.body), "NOT_IN_VOICE") == 0);
    call(&w, "POST", "/api/voice/mic", KEY, NULL, "{\"enabled\":true}");
    CHECK(g_r.status == 409 && strcmp(json_code(g_r.body), "NOT_IN_VOICE") == 0);
    call(&w, "GET", "/api/voice/join", KEY, NULL, NULL);
    CHECK(g_r.status == 405);
    call(&w, "POST", "/api/voice/nothing", KEY, NULL, "{}");
    CHECK(g_r.status == 404);
    /* JSON only */
    char buf[512];
    snprintf(buf, sizeof(buf), "POST /api/voice/leave HTTP/1.1\r\nHost: 127.0.0.1:8646\r\nX-Akeno-Native-Key: %s\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\n{}", KEY);
    pv_control_handle(w.e, &c, buf, strlen(buf), &g_r);
    CHECK(g_r.status == 415);
    call(&w, "POST", "/api/voice/exit", KEY, NULL, "{}");
    CHECK(g_r.status == 200 && g_r.exit_requested);
    /* the settings survive a restart; the microphone permission never does */
    world_close(&w);
}

static void test_settings_persist(void) {
    world w;
    world_open(&w, 1);
    pv_engine_set_volume(w.e, 130);
    pv_engine_set_gain(w.e, 70);
    pv_error err;
    CHECK(pv_engine_set_route(w.e, 4, &err) != 0 && strcmp(err.code, "ROUTE_RETIRED") == 0);
    CHECK(pv_engine_set_route(w.e, 2, &err) == 0);
    CHECK(pv_engine_set_route(w.e, 9, &err) != 0 && strcmp(err.code, "INVALID_ROUTE") == 0);
    CHECK(pv_engine_set_volume(w.e, 999) == PV_VOLUME_MAX && pv_engine_set_gain(w.e, 1) == PV_GAIN_MIN);
    pv_engine_set_volume(w.e, 130);
    pv_engine_set_gain(w.e, 70);
    pv_engine_free(w.e);
    w.e = pv_engine_new(&w.cfg);
    pv_status s;
    pv_engine_status(w.e, &s);
    CHECK(s.out_volume == 130 && s.mic_gain == 70 && s.route == 2 && s.mic_enabled == 0);
    /* a retired route saved by 0.1.0 comes back as route 1 and is never opened */
    pv_engine_free(w.e);
    char path[200];
    snprintf(path, sizeof(path), "%s/settings.json", w.data);
    write_text(path, "{\"outputVolume\":100,\"micGain\":100,\"route\":3}", 0644);
    w.e = pv_engine_new(&w.cfg);
    pv_engine_status(w.e, &s);
    CHECK(s.route == 1);
    world_close(&w);
}

/* a join right after another one ended: "Voice is still closing. Try again in a moment." */
static int join_retry(pv_engine *e, const char *party, pv_error *err) {
    for (int t = 0; t < 8000; t += 20) {
        if (pv_engine_join(e, party, err) == 0) return 0;
        if (strcmp(err->code, "BUSY") != 0) return -1;
        pv_engine_tick(e);
        sleep_ms(20);
    }
    return -1;
}

static void test_voice_pass_refusals(void) {
    fake_daemon *f = calloc(1, sizeof(*f));
    pthread_t thread;
    CHECK(fake_start(f, &thread) == 0);
    world w;
    world_open(&w, f->port);
    pv_error err;
    pv_status s;
    const char *party = "11111111-2222-4333-8444-555555555555";

    /* the production server of 2026-10-18: no device voice route */
    fake_set(f, NULL, "{\"ok\":false,\"status\":404,\"error\":{\"code\":\"NOT_FOUND\",\"message\":\"The requested endpoint was not found.\"}}");
    CHECK(join_retry(w.e, party, &err) == 0);
    /* ended and out of voice in the same update, also before the next tick */
    CHECK(wait_net_untick(w.e, PV_NET_ENDED, &s, 5000));
    CHECK(!s.in_voice && !s.mic_enabled);
    CHECK(pv_engine_set_mic(w.e, 1, &err) != 0 && strcmp(err.code, "NOT_IN_VOICE") == 0);
    CHECK(pv_engine_set_mute(w.e, 0, &err) != 0 && strcmp(err.code, "NOT_IN_VOICE") == 0);
    CHECK(wait_for(w.e, is_ended, 5000));
    pv_engine_status(w.e, &s);
    CHECK(contains(s.net_error, "no console voice yet (voice pass: HTTP 404)"));
    CHECK(fake_requests(f) == 1); /* final: not retried */
    CHECK(strcmp(f->last_key, KEY) == 0);
    CHECK(s.daemon_ipc == 3);
    pthread_mutex_lock(&w.sim.lock);
    CHECK(contains(w.sim.last_notification, "HTTP 404"));
    pthread_mutex_unlock(&w.sim.lock);
    CHECK(w.sim.mic_opens == 0);

    /* a daemon without the voice pass */
    fake_set(f, "{\"app\":\"akeno-network-console\",\"version\":\"0.5.0-alpha\",\"nativeIpc\":2}", NULL);
    CHECK(join_retry(w.e, party, &err) == 0);
    CHECK(wait_for(w.e, is_ended, 5000));
    pv_engine_status(w.e, &s);
    CHECK(contains(s.net_error, "native interface 2") && fake_requests(f) == 0);
    fake_set(f, "{\"app\":\"akeno-network-console\",\"version\":\"0.5.0-alpha\",\"nativeIpc\":3}", NULL);

    /* console voice off on the server, not a member: final */
    fake_set(f, NULL, "{\"ok\":false,\"status\":503,\"error\":{\"code\":\"VOICE_GATEWAY_UNAVAILABLE\",\"message\":\"x\"}}");
    CHECK(join_retry(w.e, party, &err) == 0);
    CHECK(wait_for(w.e, is_ended, 5000));
    pv_engine_status(w.e, &s);
    CHECK(contains(s.net_error, "Console voice is not set up") && fake_requests(f) == 1);
    fake_set(f, NULL, "{\"ok\":false,\"status\":403,\"error\":{\"code\":\"NOT_MEMBER\",\"message\":\"x\"}}");
    CHECK(join_retry(w.e, party, &err) == 0);
    CHECK(wait_for(w.e, is_ended, 5000));
    pv_engine_status(w.e, &s);
    CHECK(contains(s.net_error, "Only members"));

    /* a server error is retried */
    fake_set(f, NULL, "{\"ok\":false,\"status\":502,\"error\":{\"code\":\"UPSTREAM\",\"message\":\"x\"}}");
    CHECK(join_retry(w.e, party, &err) == 0);
    CHECK(wait_for(w.e, is_reconnecting, 5000));
    WAIT_UNTIL(w.e, fake_requests(f) >= 2, 8000);
    CHECK(fake_requests(f) >= 2);
    pv_engine_leave(w.e, "test");
    pv_engine_status(w.e, &s);
    CHECK(s.net == PV_NET_OFF && !s.in_voice);

    /* a gateway on another host: never contacted */
    fake_set(f, NULL, "{\"ok\":true,\"status\":200,\"data\":{\"url\":\"wss://evil.example/voice-gateway\",\"token\":\"eyJhbGciOiJIUzI1NiJ9.e30.secret\",\"room\":\"r\",\"identity\":\"i\"}}");
    CHECK(join_retry(w.e, party, &err) == 0);
    CHECK(wait_for(w.e, is_ended, 5000));
    pv_engine_status(w.e, &s);
    CHECK(contains(s.net_error, "not on this console's AKENO server"));

    /* nothing in the log or the report carries the pass, the key or the party */
    pv_engine_write_report(w.e);
    char path[200], text[65536];
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof(path), "%s/%s", w.data, i ? "report.txt" : "voice.log");
        FILE *fp = fopen(path, "r");
        size_t n = fp ? fread(text, 1, sizeof(text) - 1, fp) : 0;
        if (fp) fclose(fp);
        text[n] = '\0';
        CHECK(n > 100);
        CHECK(!contains(text, "eyJ") && !contains(text, "secret") && !contains(text, KEY) && !contains(text, "11111111-2222"));
    }
    world_close(&w);
    shutdown(f->fd, SHUT_RDWR);
    close(f->fd);
    pthread_join(thread, NULL);
    free(f);
}

static void test_microphone_consent(void) {
    fake_daemon *f = calloc(1, sizeof(*f));
    pthread_t thread;
    CHECK(fake_start(f, &thread) == 0);
    /* a valid pass for a gateway on the server's host that does not answer: the session reconnects */
    fake_set(f, NULL, "{\"ok\":true,\"status\":200,\"data\":{\"url\":\"wss://127.0.0.1:1/voice-gateway\",\"token\":\"eyJhbGciOiJIUzI1NiJ9.e30.x\",\"room\":\"r\",\"identity\":\"i\"}}");
    world w;
    world_open(&w, f->port);
    pv_error err;
    pv_status s;
    CHECK(pv_engine_join(w.e, "p-1", &err) == 0);
    CHECK(wait_for(w.e, is_reconnecting, 8000));
    CHECK(wait_for(w.e, out_running, 8000)); /* the speaker side runs without a connection */
    pv_engine_status(w.e, &s);
    CHECK(s.mic == PV_MIC_OFF && !s.mic_enabled && w.sim.mic_opens == 0); /* never without consent */
    CHECK(pv_engine_set_mute(w.e, 0, &err) != 0 && strcmp(err.code, "MIC_OFF") == 0);
    CHECK(pv_engine_set_mic(w.e, 1, &err) == 0);
    CHECK(wait_for(w.e, mic_active, 8000));
    CHECK(w.sim.mic_opens == 1);
    pv_engine_status(w.e, &s);
    CHECK(s.mic_rate == 16000 && s.mic_nonzero_frames > 5 && s.tx_tone_hz > 280 && s.tx_tone_hz < 320);
    CHECK(s.sent == 0); /* not connected: nothing leaves the console */
    CHECK(pv_engine_set_mute(w.e, 1, &err) == 0);
    pv_engine_status(w.e, &s);
    CHECK(s.muted && s.mic == PV_MIC_ACTIVE);
    pv_engine_set_deafen(w.e, 1);
    pv_engine_status(w.e, &s);
    CHECK(s.deafened && s.muted);
    CHECK(pv_engine_set_mute(w.e, 0, &err) == 0);
    pv_engine_status(w.e, &s);
    CHECK(!s.muted && !s.deafened); /* unmuting undeafens */
    CHECK(pv_engine_set_mic(w.e, 0, &err) == 0);
    CHECK(wait_for(w.e, mic_off, 8000));
    WAIT_UNTIL(w.e, w.sim.mic_closes == 1, 8000);
    CHECK(w.sim.mic_closes == 1);
    /* on again in the same session, then leave: closed */
    CHECK(pv_engine_set_mic(w.e, 1, &err) == 0);
    CHECK(wait_for(w.e, mic_active, 8000));
    pv_engine_leave(w.e, "test");
    WAIT_UNTIL(w.e, w.sim.mic_closes >= 2, 8000);
    CHECK(w.sim.mic_closes == 2 && w.sim.mic_opens == 2);
    /* a microphone the system refuses for an argument: the code is shown; 48 kHz is not tried (no rate problem) */
    w.sim.mic_mode = 2;
    unsigned long calls = w.sim.mic_open_calls;
    CHECK(pv_engine_join(w.e, "p-2", &err) == 0);
    CHECK(pv_engine_set_mic(w.e, 1, &err) == 0);
    CHECK(wait_for(w.e, mic_failed, 8000));
    pv_engine_status(w.e, &s);
    CHECK(s.mic == PV_MIC_FAILED && s.mic_rc == (int32_t)0x80260105 && !s.mic_enabled && s.in_voice && s.mic_refusal == PV_REFUSAL_ARGUMENT);
    CHECK(w.sim.mic_open_calls == calls + 1 && w.sim.mic_open_calls_48k == 0);
    CHECK(contains(s.mic_attempts, "16000 Hz -> 0x80260105") && !contains(s.mic_attempts, "48000"));
    /* turned on again in the same situation: answered from the refusal, the call is not repeated */
    CHECK(pv_engine_set_mic(w.e, 1, &err) != 0 && strcmp(err.code, "MIC_REFUSED") == 0 && contains(err.message, "0x80260105"));
    CHECK(w.sim.mic_open_calls == calls + 1);
    pv_engine_status(w.e, &s);
    CHECK(s.mic_avoided == 1 && s.mic == PV_MIC_FAILED && !s.mic_enabled && contains(s.mic_detail, "not tried again"));
    /* a game starts: a new situation, so exactly one more attempt; the PS5's answer to the background service */
    char bigapp[200];
    snprintf(bigapp, sizeof(bigapp), "%s/bigapp", w.root);
    snprintf(w.sim.bigapp_file, sizeof(w.sim.bigapp_file), "%s", bigapp);
    write_text(bigapp, "0x1234", 0644);
    WAIT_UNTIL(w.e, (pv_engine_status(w.e, &s), s.bigapp == 0x1234), 5000);
    w.sim.mic_mode = 4;
    CHECK(pv_engine_set_mic(w.e, 1, &err) == 0);
    CHECK(wait_for(w.e, mic_failed, 8000));
    pv_engine_status(w.e, &s);
    CHECK(s.mic_rc == (int32_t)0x809B0081 && s.mic_refusal == PV_REFUSAL_PROCESS && w.sim.mic_open_calls == calls + 2);
    CHECK(w.sim.mic_open_calls_48k == 0); /* a process refusal is not a rate problem */
    CHECK(pv_engine_set_mic(w.e, 1, &err) != 0 && strcmp(err.code, "MIC_REFUSED") == 0 && contains(err.message, "background service"));
    CHECK(w.sim.mic_open_calls == calls + 2);
    static char report[24 * 1024];
    pv_report_text(&s, report, sizeof(report));
    CHECK(contains(report, "MICROPHONE OPEN:               yes") && contains(report, "last refusal in this run: 0x809B0081: refused to this process"));
    CHECK(contains(report, "the same code from scePadOpen") && contains(report, "AKENO Native"));
    /* another game, and 16 kHz refused for its rate: 48 kHz once, and it opens */
    write_text(bigapp, "0x5678", 0644);
    WAIT_UNTIL(w.e, (pv_engine_status(w.e, &s), s.bigapp == 0x5678), 5000);
    w.sim.mic_mode = 5;
    CHECK(pv_engine_set_mic(w.e, 1, &err) == 0);
    CHECK(wait_for(w.e, mic_active, 8000));
    pv_engine_status(w.e, &s);
    CHECK(s.mic_rate == 48000 && w.sim.mic_open_calls_48k == 1 && contains(s.mic_attempts, "16000 Hz -> 0x80260103; 48000 Hz -> handle"));
    CHECK(pv_engine_set_mic(w.e, 0, &err) == 0);
    CHECK(wait_for(w.e, mic_off, 8000));
    /* no microphone library */
    w.sim.mic_mode = 3;
    CHECK(pv_engine_set_mic(w.e, 1, &err) == 0);
    for (int i = 0; i < 400; i++) {
        pv_engine_tick(w.e);
        pv_engine_status(w.e, &s);
        if (s.mic == PV_MIC_UNAVAILABLE) break;
        sleep_ms(20);
    }
    CHECK(s.mic == PV_MIC_UNAVAILABLE && s.mic_refusal == PV_REFUSAL_NO_LIBRARY);
    /* reads that start failing (the game ended: a new situation): the microphone stops, voice stays */
    unlink(bigapp);
    WAIT_UNTIL(w.e, (pv_engine_status(w.e, &s), s.bigapp <= 0), 5000);
    w.sim.mic_mode = 0;
    w.sim.mic_reads = 0;
    w.sim.mic_fail_after = 30;
    CHECK(pv_engine_set_mic(w.e, 1, &err) == 0);
    for (int i = 0; i < 600; i++) {
        pv_engine_tick(w.e);
        pv_engine_status(w.e, &s);
        if (s.mic == PV_MIC_FAILED) break;
        sleep_ms(20);
    }
    CHECK(s.mic == PV_MIC_FAILED && s.mic_read_errors >= 50 && s.in_voice);
    /* never more than PV_MIC_OPEN_LIMIT calls in one run, whatever is pressed */
    CHECK(s.mic_open_calls == PV_MIC_OPEN_LIMIT);
    CHECK(pv_engine_set_mic(w.e, 1, &err) != 0 && strcmp(err.code, "MIC_LIMIT") == 0);
    w.sim.mic_fail_after = -1;
    pv_engine_leave(w.e, "test");
    world_close(&w);
    shutdown(f->fd, SHUT_RDWR);
    close(f->fd);
    pthread_join(thread, NULL);
    free(f);
}

static void test_output_failures_and_test_sound(void) {
    fake_daemon *f = calloc(1, sizeof(*f));
    pthread_t thread;
    CHECK(fake_start(f, &thread) == 0);
    fake_set(f, NULL, "{\"ok\":true,\"status\":200,\"data\":{\"url\":\"wss://127.0.0.1:1/voice-gateway\",\"token\":\"eyJhbGciOiJIUzI1NiJ9.e30.x\",\"room\":\"r\",\"identity\":\"i\"}}");
    world w;
    world_open(&w, f->port);
    pv_error err;
    pv_status s;
    char t[160];
    /* test B, outside voice: one beep on route 1 through the voice pipeline (Opus, jitter buffer, decoder),
     * on a console-like port (a device clock drains a two-block queue), then the port closes */
    w.sim.out_mode = 3;
    CHECK(pv_engine_test_sound(w.e, &err) == 0);
    CHECK(pv_engine_test_sound(w.e, &err) != 0 && strcmp(err.code, "BUSY") == 0);
    CHECK(wait_for(w.e, test_finished, 10000));
    WAIT_UNTIL(w.e, test_idle(w.e), 5000);
    pv_engine_status(w.e, &s);
    CHECK(s.test_route == 1 && s.test_open_rc == 0 && s.test_state == PV_TEST_SUBMITTED && !s.test_in_voice);
    CHECK(s.test_encoded > 20 && s.test_decoded > 20); /* 550 ms: about 27 frames each way */
    CHECK(s.test_tone_blocks >= 5 && s.test_verified_blocks >= 5 && s.test_verified_blocks <= s.test_tone_blocks);
    CHECK(s.test_hz_min >= 836 && s.test_hz_max <= 924);
    CHECK(s.test_peak_percent >= 6 && s.test_peak_percent <= 12); /* -20 dBFS at 100 % volume, conservative */
    CHECK(s.probe_verdict == 1 && s.probe_blocks == PV_PROBE_BLOCKS && s.test_paced == 1);
    /* what the simulated device received: the same tone in both channels */
    CHECK(w.sim.out_tone_blocks + 1 >= s.test_verified_blocks && w.sim.out_tone_blocks >= 5 && !w.sim.out_stereo_mismatch);
    CHECK(w.sim.out_opens == 1 && w.sim.out_closes == 1);
    pv_status_summary(&s, t, sizeof(t));
    static char report[24 * 1024];
    pv_report_text(&s, report, sizeof(report));
    CHECK(contains(report, "Result:                   SUBMITTED") && contains(report, "Audible:                  NOT CONFIRMED"));
    CHECK(contains(report, "AUDIO SUBMITTED:               yes") && contains(report, "the background-process limit"));
    /* the volume applies to the test sound as to speech; at 0 % there is nothing to hear */
    pv_engine_set_volume(w.e, 0);
    CHECK(pv_engine_test_sound(w.e, &err) != 0 && strcmp(err.code, "VOLUME_ZERO") == 0);
    pv_engine_set_volume(w.e, 100);
    /* route 2, the foreground user's port, refused as on the PS5: not played, said so, and not repeated */
    w.sim.out_mode = 4;
    CHECK(pv_engine_set_route(w.e, 2, &err) == 0);
    unsigned long open_calls = w.sim.out_open_calls;
    CHECK(pv_engine_test_sound(w.e, &err) == 0);
    CHECK(wait_for(w.e, test_finished, 10000));
    WAIT_UNTIL(w.e, test_idle(w.e), 5000);
    pv_engine_status(w.e, &s);
    CHECK(s.test_state == PV_TEST_NOT_PLAYED && s.test_open_rc == (int32_t)0x80260011 && w.sim.out_open_calls == open_calls + 1);
    pv_report_text(&s, report, sizeof(report));
    CHECK(contains(report, "NOT PLAYED") && !contains(report, "Result:                   SUBMITTED"));
    CHECK(contains(report, "was refused with 0x80260011"));
    CHECK(pv_engine_test_sound(w.e, &err) == 0);
    CHECK(wait_for(w.e, test_finished, 10000));
    WAIT_UNTIL(w.e, test_idle(w.e), 5000);
    pv_engine_status(w.e, &s);
    CHECK(s.test_state == PV_TEST_NOT_PLAYED && w.sim.out_open_calls == open_calls + 1 && s.out_avoided == 1);
    CHECK(contains(s.test_detail, "not tried again"));
    CHECK(pv_engine_set_route(w.e, 1, &err) == 0);
    /* an output port the system refuses: incoming audio is still decoded in real time, and it says so */
    w.sim.out_mode = 2;
    CHECK(pv_engine_join(w.e, "p-1", &err) == 0);
    CHECK(wait_for(w.e, out_failed, 8000));
    pv_engine_status(w.e, &s);
    CHECK(s.out_rc == (int32_t)0x8026000A && s.out_calls == 0);
    pv_status_summary(&s, t, sizeof(t));
    CHECK(!contains(t, "Listening"));
    /* no port, no test sound: refused at once, never "mixed in" */
    CHECK(pv_engine_test_sound(w.e, &err) != 0 && strcmp(err.code, "OUTPUT_UNAVAILABLE") == 0 && contains(err.message, "0x8026000A"));
    pv_engine_leave(w.e, "test");
    /* a port that takes nothing (returns at once): the probe and the calls say NOT paced; the guard keeps real time */
    w.sim.out_mode = 1;
    CHECK(pv_engine_join(w.e, "p-1", &err) == 0);
    sleep_ms(1500);
    pv_engine_tick(w.e);
    pv_engine_status(w.e, &s);
    CHECK(s.paced == 0 && s.probe_verdict == 0 && s.out_calls > 25 && s.out_calls < 120); /* ~70 in 1.5 s, not thousands */
    CHECK(s.out_self_sleeps > 5);
    pv_engine_leave(w.e, "test");
    /* the console-like port: it paces every block itself, the thread never sleeps on it */
    w.sim.out_mode = 3;
    unsigned long before = s.out_calls;
    CHECK(pv_engine_join(w.e, "p-1", &err) == 0);
    sleep_ms(1500);
    pv_engine_tick(w.e);
    pv_engine_status(w.e, &s);
    /* 1.5 s: the probe (12 blocks, ~0.25 s), then ~58 blocks paced by the port */
    CHECK(s.paced == 1 && s.probe_verdict == 1 && s.out_self_sleeps == 0 && s.out_calls - before > 40 && s.out_calls - before < 90);
    /* the route changes while in voice: the port is reopened on it. Route 2 was refused in this
     * situation (above), so it is only tried again once a game runs. */
    unsigned long opens = w.sim.out_opens;
    CHECK(pv_engine_set_route(w.e, 2, &err) == 0);
    WAIT_UNTIL(w.e, (pv_engine_status(w.e, &s), s.out == PV_OUT_FAILED), 5000);
    CHECK(s.out == PV_OUT_FAILED && contains(s.out_detail, "not tried again") && w.sim.out_opens == opens);
    char bigapp[200];
    snprintf(bigapp, sizeof(bigapp), "%s/bigapp", w.root);
    snprintf(w.sim.bigapp_file, sizeof(w.sim.bigapp_file), "%s", bigapp);
    write_text(bigapp, "0x4321", 0644);
    WAIT_UNTIL(w.e, (pv_engine_status(w.e, &s), s.bigapp == 0x4321), 5000);
    CHECK(pv_engine_set_route(w.e, 1, &err) == 0);
    WAIT_UNTIL(w.e, w.sim.out_opens == opens + 1, 5000);
    CHECK(pv_engine_set_route(w.e, 2, &err) == 0);
    opens++;
    WAIT_UNTIL(w.e, w.sim.out_opens == opens + 1, 5000);
    CHECK(w.sim.out_opens == opens + 1);
    /* output calls that start failing: the port is given up, voice stays */
    w.sim.out_mode = 0;
    w.sim.out_calls = 0;
    w.sim.out_fail_after = 5;
    CHECK(pv_engine_set_route(w.e, 1, &err) == 0);
    for (int i = 0; i < 400; i++) {
        pv_engine_tick(w.e);
        pv_engine_status(w.e, &s);
        if (s.out == PV_OUT_FAILED) break;
        sleep_ms(20);
    }
    CHECK(s.out == PV_OUT_FAILED && s.out_errors >= 50 && s.in_voice);
    /* a test sound in voice goes through the same pipeline and is mixed into the voice output */
    w.sim.out_fail_after = -1;
    w.sim.out_mode = 3;
    w.sim.out_tone_blocks = 0;
    CHECK(pv_engine_set_route(w.e, 2, &err) == 0);
    WAIT_UNTIL(w.e, (pv_engine_status(w.e, &s), s.out == PV_OUT_ACTIVE && s.route == 2), 5000);
    CHECK(pv_engine_test_sound(w.e, &err) == 0);
    CHECK(wait_for(w.e, test_finished, 10000));
    pv_engine_status(w.e, &s);
    CHECK(s.test_state == PV_TEST_SUBMITTED && s.test_in_voice && s.test_route == 2 && s.test_verified_blocks >= 5);
    CHECK(s.test_sounds == 4 && w.sim.out_tone_blocks >= 5 && !w.sim.out_stereo_mismatch);
    CHECK(s.out_speech_blocks == 0); /* no party speech: the tone is not counted as speech */
    pv_engine_leave(w.e, "test");
    world_close(&w);
    shutdown(f->fd, SHUT_RDWR);
    close(f->fd);
    pthread_join(thread, NULL);
    free(f);
}

/* What a console code means (pv_refusal_of): PS4 SDK names via shadPS4's tables, 0x809B0081 by observation. */
static void test_classification(void) {
    CHECK(pv_refusal_of((int32_t)0x809B0081, 1) == PV_REFUSAL_PROCESS && pv_refusal_of((int32_t)0x809B0081, 0) == PV_REFUSAL_PROCESS);
    CHECK(pv_refusal_of((int32_t)0x80260103, 1) == PV_REFUSAL_RATE);     /* audio in INVALID_FREQ */
    CHECK(pv_refusal_of((int32_t)0x80260105, 1) == PV_REFUSAL_ARGUMENT); /* audio in INVALID_POINTER */
    CHECK(pv_refusal_of((int32_t)0x80260107, 1) == PV_REFUSAL_BUSY && pv_refusal_transient(PV_REFUSAL_BUSY));
    CHECK(pv_refusal_of((int32_t)0x80260011, 0) == PV_REFUSAL_RESOURCE); /* audio out SYSTEM_RESOURCE (route 2 on 12.20) */
    CHECK(pv_refusal_of((int32_t)0x8026000A, 0) == PV_REFUSAL_ARGUMENT); /* audio out INVALID_PORT_TYPE */
    CHECK(pv_refusal_of((int32_t)0x80260008, 0) == PV_REFUSAL_RATE);
    CHECK(pv_refusal_of(PV_NO_LIBRARY, 1) == PV_REFUSAL_NO_LIBRARY && pv_refusal_of(PV_NO_USER_SERVICE, 1) == PV_REFUSAL_NO_USER);
    CHECK(pv_refusal_of((int32_t)0x80960005, 1) == PV_REFUSAL_NO_USER); /* user service codes */
    CHECK(pv_refusal_of(0, 1) == PV_REFUSAL_NONE && pv_refusal_of(7, 0) == PV_REFUSAL_NONE);
    CHECK(pv_refusal_of((int32_t)0x80020001, 1) == PV_REFUSAL_OTHER);
    CHECK(!pv_refusal_transient(PV_REFUSAL_PROCESS) && !pv_refusal_transient(PV_REFUSAL_ARGUMENT) && !pv_refusal_transient(PV_REFUSAL_RESOURCE));
    CHECK(contains(pv_refusal_meaning(PV_REFUSAL_PROCESS), "scePadOpen") && strcmp(pv_refusal_name(PV_REFUSAL_PROCESS), "process") == 0);
    /* the clock verdict: time inside calls 2..n against the audio they carried */
    double block = (double)PV_OUT_GRAIN * 1000.0 / PV_OUT_RATE;
    CHECK(pv_clock_verdict(11 * block, 11 * block) == 1);
    CHECK(pv_clock_verdict(9 * block, 11 * block) == 1);  /* a queue of two or three blocks */
    CHECK(pv_clock_verdict(0.5, 11 * block) == 0);         /* returned at once */
    CHECK(pv_clock_verdict(4 * block, 11 * block) == -1);  /* in between: undecided, never a guess */
    CHECK(pv_clock_verdict(5 * block, 5 * block) == -1);   /* too few calls */
}

/* The "NOT paced" of the owner's 12.20 log, reproduced on a virtual clock (deterministic): a port
 * whose device clock drains a two-block queue in real time, fed by 0.1.0's loop and by 0.2.0's.
 * 0.1.0 slept until its next block time after every call that returned within 2 ms. A console port
 * takes its first block at once, so 0.1.0 slept, the queue drained meanwhile, the next call found
 * room and returned at once again: "NOT paced" for every call, although the clock consumed every
 * block. (Its loop has a second regime: once one call blocks >= 2 ms it stops sleeping and stays
 * back to back. The owner's log - near-zero call times for the whole session - is the first.) */
typedef struct {
    double now, end; /* ms: the caller's time, and when the queued audio has played */
} vport;

static double vport_output(vport *p, double block_ms) {
    if (p->end < p->now) p->end = p->now; /* ran dry: the device played silence meanwhile */
    p->end += block_ms;
    double room_at = p->end - block_ms; /* a two-block queue: blocks until one block has played */
    double wait = room_at > p->now ? room_at - p->now : 0;
    p->now += wait + 0.05; /* the call itself */
    return wait + 0.05;
}

static void test_pacing_artifact(void) {
    const double block_ms = (double)PV_OUT_GRAIN * 1000.0 / PV_OUT_RATE;
    for (int strategy = 0; strategy < 2; strategy++) {
        vport p = { 0, 0 };
        double inside = 0, all = 0, next = 0;
        for (int i = 0; i < 200; i++) {
            p.now += 0.4; /* decoding and mixing a block */
            double dt = vport_output(&p, block_ms);
            all += dt;
            if (i > 0) inside += dt;
            if (strategy == 0) { /* 0.1.0's pace_by_clock(), with pv_sleep_ms's rounding up to whole ms */
                if (dt < 2.0) {
                    next += block_ms;
                    if (next > p.now) p.now += ceil(next - p.now);
                    else if (p.now - next > 200.0) next = p.now;
                } else {
                    next = p.now;
                }
            }
        }
        double avg = all / 200.0; /* 0.1.0's verdict: paced when the average call took 0.6..1.6 blocks */
        int old_paced = avg > block_ms * 0.6 && avg < block_ms * 1.6;
        int verdict = pv_clock_verdict(inside, 199 * block_ms);
        if (strategy == 0) CHECK(!old_paced && verdict == 0 && avg < 1.0); /* "NOT paced", near-zero call times */
        else CHECK(old_paced && verdict == 1);                             /* back to back: the port's blocking shows it */
        printf("pacing against a two-block queue port (virtual clock): %s: %.2f ms per call on average\n",
               strategy == 0 ? "0.1.0's sleep-when-quick" : "0.2.0's back to back", avg);
    }
    /* the same port in real time through the simulated console: back to back, it blocks a block per call */
    pv_sim sim;
    pv_backend be;
    pv_sim_from_env(&sim);
    sim.out_mode = 3;
    pv_backend_host(&be, &sim);
    static int16_t block[2 * PV_OUT_GRAIN];
    char why[160];
    int32_t h = be.out_open(be.user, 1, why, sizeof(why));
    CHECK(h >= 0);
    double inside = 0;
    for (int i = 0; i < 24; i++) {
        uint64_t t0 = be.now_us(be.user);
        CHECK(be.out_output(be.user, h, block) == 0);
        if (i > 0) inside += (double)(be.now_us(be.user) - t0) / 1000.0;
    }
    CHECK(pv_clock_verdict(inside, 23 * block_ms) == 1);
    be.out_close(be.user, h);
}

/* This process's context, decoded by AKENO Native's context.c from the console's raw answers. */
static void test_process_context(void) {
    world w;
    world_open(&w, 1);
    pv_status s;
    pv_engine_status(w.e, &s);
    CHECK(s.process_kind == AKN_CTX_BACKGROUND && contains(s.process_detail, "background process"));
    w.sim.context = 1;
    pv_refresh_process(w.e);
    pv_engine_status(w.e, &s);
    CHECK(s.process_kind == AKN_CTX_FOREGROUND && contains(s.process_detail, "foreground application"));
    w.sim.context = 2;
    pv_refresh_process(w.e);
    pv_engine_status(w.e, &s);
    CHECK(s.process_kind == AKN_CTX_UNKNOWN && contains(s.process_detail, "unavailable"));
    world_close(&w);
}

/* PCM formats on the way to the port: interleaving, volume, saturation, frame counts. */
static void test_pcm_format(void) {
    int16_t mono[4] = { 1000, -1000, 32767, -32768 }, st[8];
    akn_pcm_mono_to_stereo(mono, 4, 100, st);
    CHECK(st[0] == 1000 && st[1] == 1000 && st[2] == -1000 && st[3] == -1000);
    akn_pcm_mono_to_stereo(mono, 4, 150, st);
    CHECK(st[4] == 32767 && st[5] == 32767 && st[6] == -32768 && st[7] == -32768); /* saturates, never wraps */
    akn_pcm_mono_to_stereo(mono, 4, 0, st);
    CHECK(st[0] == 0 && st[7] == 0);
    CHECK(PV_OUT_GRAIN * 2 * (int)sizeof(int16_t) == 4096); /* one S16 stereo block of 1024 frames */
    /* an Opus round trip of the test beep keeps its frequency (the pipeline the test sound uses) */
    akn_voice_encoder *enc = akn_voice_encoder_new(NULL);
    akn_voice_decoder *dec = akn_voice_decoder_new(NULL);
    akn_lab_beeps beeps;
    akn_lab_beeps_init(&beeps, AKN_VOICE_RATE, 1, PV_TEST_AMPLITUDE);
    static int16_t stereo[2 * AKN_VOICE_FRAME];
    int16_t pcm[AKN_VOICE_FRAME], out[AKN_VOICE_FRAME];
    uint8_t packet[AKN_VOICE_MAX_PACKET];
    int good = 0;
    for (int f = 0; f < 6; f++) {
        akn_lab_beeps_fill(&beeps, stereo, AKN_VOICE_FRAME);
        for (int i = 0; i < AKN_VOICE_FRAME; i++) pcm[i] = stereo[2 * i];
        int len = akn_voice_encode(enc, pcm, packet, sizeof(packet));
        CHECK(len > 0);
        CHECK(akn_voice_decode(dec, packet, (size_t)len, out) == AKN_VOICE_FRAME);
        int hz = akn_pcm_tone_hz(out, AKN_VOICE_FRAME, AKN_VOICE_RATE);
        if (f >= 1 && hz >= 836 && hz <= 924) good++;
    }
    CHECK(good >= 4);
    akn_voice_encoder_free(enc);
    akn_voice_decoder_free(dec);
}

int main(void) {
    test_basics();
    test_classification();
    test_pacing_artifact();
    test_pcm_format();
    test_process_context();
    test_summary_and_evidence();
    test_status_json();
    test_report_privacy();
    test_control_security();
    test_settings_persist();
    test_voice_pass_refusals();
    test_microphone_consent();
    test_output_failures_and_test_sound();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
