/* Party voice core: Opus codec, jitter buffer, sample-rate conversion, levels, the gateway
 * protocol and the session model. */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "jitter.h"
#include "test.h"
#include "voice.h"
#include "voice_codec.h"
#include "voice_dsp.h"
#include "voice_proto.h"

static void sine(int16_t *out, size_t n, double hz, int rate, double amp, double *phase) {
    for (size_t i = 0; i < n; i++) {
        out[i] = (int16_t)lrint(amp * sin(*phase));
        *phase += 2.0 * 3.14159265358979323846 * hz / rate;
    }
}

static void test_codec(void) {
    int err = 0;
    akn_voice_encoder *e = akn_voice_encoder_new(&err);
    akn_voice_decoder *d = akn_voice_decoder_new(&err);
    CHECK(e && d && err == 0);
    if (!e || !d) return;
    int16_t pcm[AKN_VOICE_FRAME], out[AKN_VOICE_FRAME];
    uint8_t pkt[AKN_VOICE_MAX_PACKET];
    double ph = 0;
    int last_tone = 0, max_len = 0;
    double last_db = -100;
    for (int f = 0; f < 25; f++) {
        sine(pcm, AKN_VOICE_FRAME, 440, AKN_VOICE_RATE, 8000, &ph);
        int n = akn_voice_encode(e, pcm, pkt, sizeof(pkt));
        CHECK(n > 0 && n <= AKN_VOICE_MAX_PACKET);
        if (n > max_len) max_len = n;
        CHECK(akn_voice_decode(d, pkt, (size_t)n, out) == AKN_VOICE_FRAME);
        last_tone = akn_pcm_tone_hz(out, AKN_VOICE_FRAME, AKN_VOICE_RATE);
        last_db = akn_pcm_rms_dbfs(out, AKN_VOICE_FRAME);
    }
    CHECK(last_tone >= 430 && last_tone <= 450);           /* the tone survives the codec */
    CHECK(fabs(last_db - akn_pcm_rms_dbfs(pcm, AKN_VOICE_FRAME)) < 3.0);
    CHECK(max_len <= 120);                                 /* about 32 kbit/s */
    /* a lost packet is concealed with a full frame */
    CHECK(akn_voice_decode(d, NULL, 0, out) == AKN_VOICE_FRAME);
    /* garbage does not crash and yields silence or concealment */
    uint8_t junk[50];
    for (int i = 0; i < 50; i++) junk[i] = (uint8_t)(i * 37 + 11);
    int r = akn_voice_decode(d, junk, sizeof(junk), out);
    CHECK(r == AKN_VOICE_FRAME || r < 0);
    CHECK(akn_voice_decode(d, pkt, AKN_VOICE_MAX_PACKET + 1, out) == AKN_VOICE_FRAME); /* oversized: concealed */
    CHECK(akn_voice_encode(e, NULL, pkt, sizeof(pkt)) < 0);
    CHECK(strstr(akn_voice_codec_version(), "1.5.2") != NULL);
    akn_voice_encoder_free(e);
    akn_voice_decoder_free(d);
}

static void put(akn_jitter *j, uint16_t seq) {
    uint8_t b[3] = { (uint8_t)(seq >> 8), (uint8_t)seq, 7 };
    akn_jitter_put(j, seq, b, sizeof(b));
}

static akn_jitter_result get(akn_jitter *j, int *seq_out) {
    uint8_t out[AKN_VOICE_MAX_PACKET];
    size_t len = 0;
    akn_jitter_result r = akn_jitter_get(j, out, &len);
    if (r == AKN_JITTER_PACKET) *seq_out = (out[0] << 8) | out[1];
    return r;
}

static void test_jitter(void) {
    akn_jitter *j = calloc(1, sizeof(*j));
    int seq = -1;
    akn_jitter_init(j);
    put(j, 100);
    put(j, 101);
    CHECK(get(j, &seq) == AKN_JITTER_EMPTY);            /* prefill */
    put(j, 103);                                        /* 102 is missing for now */
    CHECK(get(j, &seq) == AKN_JITTER_PACKET && seq == 100);
    put(j, 102);                                        /* arrives late but in time */
    CHECK(get(j, &seq) == AKN_JITTER_PACKET && seq == 101);
    CHECK(get(j, &seq) == AKN_JITTER_PACKET && seq == 102);
    CHECK(get(j, &seq) == AKN_JITTER_PACKET && seq == 103);
    put(j, 101);                                        /* already played: late */
    CHECK(j->late == 1);
    put(j, 105);
    put(j, 105);                                        /* duplicate */
    CHECK(j->duplicates == 1);
    CHECK(get(j, &seq) == AKN_JITTER_LOST);              /* 104 lost: conceal */
    CHECK(get(j, &seq) == AKN_JITTER_PACKET && seq == 105);
    CHECK(get(j, &seq) == AKN_JITTER_EMPTY && j->underruns == 1); /* stream stopped: rebuffer */
    /* overflow: the delay stays bounded */
    for (int i = 0; i < 25; i++) put(j, (uint16_t)(200 + i));
    CHECK(akn_jitter_depth(j) <= AKN_JITTER_MAX_DEPTH);
    CHECK(get(j, &seq) == AKN_JITTER_PACKET && seq == 215);
    /* sequence wrap */
    akn_jitter_init(j);
    put(j, 65534);
    put(j, 65535);
    put(j, 0);
    put(j, 1);
    CHECK(get(j, &seq) == AKN_JITTER_PACKET && seq == 65534);
    CHECK(get(j, &seq) == AKN_JITTER_PACKET && seq == 65535);
    CHECK(get(j, &seq) == AKN_JITTER_PACKET && seq == 0);
    CHECK(get(j, &seq) == AKN_JITTER_PACKET && seq == 1);
    /* bad input */
    uint8_t big[AKN_VOICE_MAX_PACKET + 1] = { 0 };
    CHECK(akn_jitter_put(j, 5, big, sizeof(big)) == 1);
    CHECK(akn_jitter_put(j, 5, NULL, 3) == 1);
    /* a long run of losses is not concealed forever */
    akn_jitter_init(j);
    put(j, 10);
    put(j, 11);
    put(j, 12);
    put(j, 30);
    for (int i = 0; i < 3; i++) CHECK(get(j, &seq) == AKN_JITTER_PACKET);
    int lost = 0;
    akn_jitter_result r;
    while ((r = get(j, &seq)) == AKN_JITTER_LOST) lost++;
    CHECK(lost == AKN_JITTER_MAX_CONCEAL && r == AKN_JITTER_EMPTY);
    free(j);
}

static void test_dsp(void) {
    /* 16 kHz -> 48 kHz keeps a 1 kHz tone and its level */
    akn_upsample3 u;
    akn_upsample3_init(&u);
    int16_t in[256], out[768], all[768 * 8];
    double ph = 0;
    for (int b = 0; b < 8; b++) {
        sine(in, 256, 1000, 16000, 10000, &ph);
        akn_upsample3_run(&u, in, 256, out);
        memcpy(all + b * 768, out, sizeof(out));
    }
    int hz = akn_pcm_tone_hz(all + 768, 768 * 7, 48000);
    CHECK(hz >= 990 && hz <= 1010);
    CHECK(fabs(akn_pcm_rms_dbfs(all + 768, 768 * 7) - akn_pcm_rms_dbfs(in, 256)) < 1.0);
    /* a 7 kHz tone (near the 16 kHz Nyquist) does not alias into the audible band below it */
    akn_upsample3_init(&u);
    ph = 0;
    sine(in, 256, 3000, 16000, 10000, &ph);
    akn_upsample3_run(&u, in, 256, out);
    hz = akn_pcm_tone_hz(out + 96, 768 - 96, 48000);
    CHECK(hz >= 2950 && hz <= 3050);

    int16_t g[4] = { 1000, -1000, 30000, -30000 };
    CHECK(akn_pcm_gain(g, 4, 200) == 2);
    CHECK(g[0] == 2000 && g[1] == -2000 && g[2] == 32767 && g[3] == -32768);
    akn_pcm_gain(g, 4, 0);
    CHECK(g[0] == 0 && g[2] == 0);
    CHECK(akn_pcm_rms_dbfs(g, 4) == -100.0);
    CHECK(akn_meter_percent(-100) == 0 && akn_meter_percent(0) == 100 && akn_meter_percent(-30) == 50);
    int16_t m[2] = { 100, -32768 }, st[4];
    akn_pcm_mono_to_stereo(m, 2, 50, st);
    CHECK(st[0] == 50 && st[1] == 50 && st[2] == -16384 && st[3] == -16384);
    CHECK(akn_pcm_peak(m, 2) == 32767);

    akn_frame_acc a;
    akn_frame_acc_init(&a);
    int ready = 0;
    int16_t blk[768] = { 0 };
    CHECK(akn_frame_acc_add(&a, blk, 768, &ready) == 768 && !ready);
    CHECK(akn_frame_acc_add(&a, blk, 768, &ready) == 192 && ready);
    akn_frame_acc_init(&a);
    CHECK(a.fill == 0);

    akn_pcm_fifo *f = malloc(sizeof(*f));
    akn_pcm_fifo_init(f);
    int16_t chunk[1000];
    for (int i = 0; i < 1000; i++) chunk[i] = (int16_t)i;
    size_t w = 0;
    for (int i = 0; i < 12; i++) w += akn_pcm_fifo_write(f, chunk, 1000);
    CHECK(w == AKN_PCM_FIFO);                             /* bounded */
    int16_t back[1000];
    CHECK(akn_pcm_fifo_read(f, back, 1000) == 1000 && back[999] == 999);
    free(f);
    int16_t quiet[480] = { 0 };
    CHECK(akn_pcm_tone_hz(quiet, 480, 48000) == 0);
}

static void test_proto(void) {
    char buf[4096];
    int n = akn_voice_hello_json("abc.def.ghi", "akeno-native/1.1", 1, 0, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK_STR(buf, "{\"type\":\"hello\",\"protocol\":1,\"token\":\"abc.def.ghi\",\"client\":\"akeno-native/1.1\",\"muted\":true,\"deafened\":false}");
    CHECK(akn_voice_hello_json("", "x", 0, 0, buf, sizeof(buf)) < 0);
    CHECK(akn_voice_hello_json("tok\"en", "x", 0, 0, buf, sizeof(buf)) > 0 && strstr(buf, "tok\\\"en"));
    CHECK(akn_voice_hello_json("t", "x", 0, 0, buf, 20) < 0);
    CHECK(akn_voice_flag_json("mute", "muted", 1, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "{\"type\":\"mute\",\"muted\":true}");
    CHECK(akn_voice_ping_json(7, buf, sizeof(buf)) > 0);
    CHECK_STR(buf, "{\"type\":\"ping\",\"id\":7}");

    akn_voice_msg *m = malloc(sizeof(*m));
    const char *ready = "{\"type\":\"ready\",\"protocol\":1,\"room\":\"akeno-party-x\",\"identity\":\"u1\",\"sampleRate\":48000,\"channels\":1,\"frameMs\":20}";
    CHECK(akn_voice_parse(ready, strlen(ready), m) == 0 && m->type == AKN_VMSG_READY && akn_voice_ready_ok(m));
    CHECK_STR(m->identity, "u1");
    const char *ready2 = "{\"type\":\"ready\",\"protocol\":2,\"sampleRate\":48000,\"channels\":1,\"frameMs\":20}";
    CHECK(akn_voice_parse(ready2, strlen(ready2), m) == 0 && !akn_voice_ready_ok(m));
    const char *people = "{\"type\":\"participants\",\"list\":[{\"id\":\"u1\",\"name\":\"Me\",\"self\":true,\"speaking\":true,\"level\":250},"
                         "{\"id\":\"u2\",\"name\":\"Bob\",\"muted\":true},{\"name\":\"no id\"},7]}";
    CHECK(akn_voice_parse(people, strlen(people), m) == 0 && m->type == AKN_VMSG_PARTICIPANTS && m->count == 2);
    CHECK(m->list[0].self && m->list[0].speaking && m->list[0].level == 100 && m->list[1].muted);
    akn_voice v;
    akn_voice_init(&v);
    akn_voice_set_people(&v, m);
    CHECK(v.self_speaking && akn_voice_in_room(&v, "u2") && !akn_voice_is_speaking(&v, "u2") && akn_voice_is_speaking(&v, "u1"));
    CHECK(!akn_voice_in_room(&v, "u3") && !akn_voice_in_room(&v, ""));
    const char *err = "{\"type\":\"error\",\"code\":\"TOKEN_INVALID\",\"message\":\"no\"}";
    CHECK(akn_voice_parse(err, strlen(err), m) == 0 && m->type == AKN_VMSG_ERROR);
    CHECK_STR(m->code, "TOKEN_INVALID");
    const char *st = "{\"type\":\"state\",\"state\":\"reconnecting\"}";
    CHECK(akn_voice_parse(st, strlen(st), m) == 0 && m->type == AKN_VMSG_STATE && m->reconnecting);
    const char *pong = "{\"type\":\"pong\",\"id\":42}";
    CHECK(akn_voice_parse(pong, strlen(pong), m) == 0 && m->type == AKN_VMSG_PONG && m->id == 42);
    CHECK(akn_voice_parse("[1]", 3, m) < 0);
    CHECK(akn_voice_parse("{\"type\":\"x\"}", 12, m) == 0 && m->type == AKN_VMSG_UNKNOWN);
    CHECK(akn_voice_parse("{", 1, m) < 0);
    free(m);

    uint8_t opus[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }, frame[AKN_VOICE_MAX_PACKET + 3];
    n = akn_voice_pack_audio(0xBEEF, opus, sizeof(opus), frame, sizeof(frame));
    CHECK(n == 13 && frame[0] == 1 && frame[1] == 0xBE && frame[2] == 0xEF);
    uint16_t seq;
    const uint8_t *p;
    size_t plen;
    CHECK(akn_voice_unpack_audio(frame, (size_t)n, &seq, &p, &plen) == 0 && seq == 0xBEEF && plen == 10 && p[9] == 10);
    frame[0] = 2;
    CHECK(akn_voice_unpack_audio(frame, (size_t)n, &seq, &p, &plen) < 0);
    CHECK(akn_voice_unpack_audio(frame, 3, &seq, &p, &plen) < 0);
    CHECK(akn_voice_pack_audio(1, opus, AKN_VOICE_MAX_PACKET + 1, frame, sizeof(frame)) < 0);
    CHECK(akn_voice_pack_audio(1, opus, 10, frame, 12) < 0);

    akn_voice_credential *c = malloc(sizeof(*c));
    const char *cred = "{\"data\":{\"url\":\"wss://akeno.example/voice-gateway\",\"token\":\"t.t.t\",\"room\":\"akeno-party-1\",\"identity\":\"u1\"}}";
    CHECK(akn_voice_parse_credential(cred, strlen(cred), c) == 0);
    CHECK_STR(c->url, "wss://akeno.example/voice-gateway");
    CHECK_STR(c->token, "t.t.t");
    const char *plain = "{\"url\":\"ws://akeno.example/voice-gateway\",\"token\":\"t\"}";
    CHECK(akn_voice_parse_credential(plain, strlen(plain), c) < 0);   /* never without TLS */
    const char *notoken = "{\"url\":\"wss://a/b\"}";
    CHECK(akn_voice_parse_credential(notoken, strlen(notoken), c) < 0);
    free(c);
    CHECK(akn_voice_same_host("wss://akeno.example:8443/voice-gateway", "https://akeno.example"));
    CHECK(akn_voice_same_host("wss://AKENO.example/x", "https://akeno.example:443/"));
    CHECK(!akn_voice_same_host("wss://akeno.example.evil.net/x", "https://akeno.example"));
    CHECK(akn_voice_same_host("wss://attacker@akeno.example/x", "https://akeno.example"));
    CHECK(!akn_voice_same_host("wss://akeno.example@evil.net/x", "https://akeno.example"));
    CHECK(akn_voice_same_host("wss://[::1]:9/x", "https://[::1]:8/"));
    CHECK(!akn_voice_same_host("nonsense", "https://akeno.example"));
    CHECK(strstr(akn_voice_close_text(AKN_VOICE_CLOSE_REMOVED), "removed") != NULL);
    CHECK(strstr(akn_voice_error_text("NOT_MEMBER"), "members") != NULL);
}

static void test_model(void) {
    akn_voice v;
    akn_voice_init(&v);
    CHECK(v.out_volume == 100 && v.mic_volume == 100 && v.state == AKN_VOICE_OFF);
    CHECK_STR(akn_voice_state_text(&v), "Not in voice");
    v.state = AKN_VOICE_CONNECTED;
    v.muted = 1;
    CHECK_STR(akn_voice_state_text(&v), "In voice (muted)");
    v.deafened = 1;
    CHECK_STR(akn_voice_state_text(&v), "In voice (deafened)");
    CHECK(akn_voice_volume_step(100, 1) == 110 && akn_voice_volume_step(200, 1) == 200 && akn_voice_volume_step(0, -1) == 0);
    CHECK(akn_voice_backoff_ms(1) == 1000 && akn_voice_backoff_ms(2) == 2000 && akn_voice_backoff_ms(4) == 8000 && akn_voice_backoff_ms(9) == 10000);
    CHECK(akn_voice_should_retry(1006, 0) && akn_voice_should_retry(AKN_VOICE_CLOSE_AUTH, 1));
    CHECK(!akn_voice_should_retry(AKN_VOICE_CLOSE_REMOVED, 0) && !akn_voice_should_retry(AKN_VOICE_CLOSE_REPLACED, 0));
    CHECK(!akn_voice_should_retry(AKN_VOICE_CLOSE_ENDED, 0) && !akn_voice_should_retry(1000, 0));
    CHECK(!akn_voice_should_retry(1006, AKN_VOICE_RECONNECT_MAX));
    CHECK_STR(akn_mic_state_text(AKN_MIC_UNAVAILABLE), "No microphone function on this system");
}

void run_voice_tests(void) {
    test_codec();
    test_jitter();
    test_dsp();
    test_proto();
    test_model();
}
