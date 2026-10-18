/* AKENO Voice Lab unit tests: PCM measurements, test signals and whole runs on a simulated console. */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "backend.h"
#include "lab.h"
#include "lab_net.h"
#include "lab_pcm.h"

static int g_checks, g_failures;

#define CHECK(cond)                                                                                                   \
    do {                                                                                                              \
        g_checks++;                                                                                                   \
        if (!(cond)) {                                                                                                \
            g_failures++;                                                                                             \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);                                  \
        }                                                                                                             \
    } while (0)

static void test_stats(void) {
    akn_lab_mic_stats s;
    int16_t block[256];

    akn_lab_mic_stats_init(&s, 16000);
    CHECK(akn_lab_mic_verdict(&s) == AKN_LAB_PCM_NONE);
    CHECK(akn_lab_mic_rms_db(&s) == -120.0);

    memset(block, 0, sizeof(block));
    for (int i = 0; i < 100; i++) akn_lab_mic_stats_add(&s, block, 256);
    CHECK(akn_lab_mic_verdict(&s) == AKN_LAB_PCM_ALL_ZERO);
    CHECK(s.zero_run_max == 25600);
    CHECK(s.windows == 25600 / 320);
    CHECK(s.windows_active == 0);

    akn_lab_mic_stats_init(&s, 16000);
    for (int i = 0; i < 256; i++) block[i] = 40;
    for (int i = 0; i < 10; i++) akn_lab_mic_stats_add(&s, block, 256);
    CHECK(akn_lab_mic_verdict(&s) == AKN_LAB_PCM_CONSTANT);

    /* quiet noise: +-3 LSB is about -80 dBFS, below the -45 dBFS speech threshold */
    akn_lab_mic_stats_init(&s, 16000);
    for (int i = 0; i < 256; i++) block[i] = (int16_t)((i % 7) - 3);
    for (int i = 0; i < 20; i++) akn_lab_mic_stats_add(&s, block, 256);
    CHECK(akn_lab_mic_verdict(&s) == AKN_LAB_PCM_NOISE_ONLY);
    CHECK(akn_lab_mic_rms_db(&s) < -60.0);

    /* a -12 dBFS tone */
    akn_lab_mic_stats_init(&s, 16000);
    unsigned long n = 0;
    for (int b = 0; b < 63; b++) {
        for (int i = 0; i < 256; i++, n++) block[i] = (int16_t)(8192.0 * sin(2 * 3.14159265358979 * 440.0 * (double)n / 16000.0));
        akn_lab_mic_stats_add(&s, block, 256);
    }
    CHECK(akn_lab_mic_verdict(&s) == AKN_LAB_PCM_SOUND);
    CHECK(s.windows_active == s.windows);
    CHECK(fabs(akn_lab_mic_rms_db(&s) - (-15.05)) < 0.3); /* -12 dBFS peak sine: RMS 3 dB lower */
    CHECK(s.peak >= 8180 && s.peak <= 8192);
    CHECK(strcmp(akn_lab_pcm_verdict_name(AKN_LAB_PCM_NOISE_ONLY), "QUIET_SIGNAL") == 0);
}

static void test_beeps(void) {
    for (unsigned count = 1; count <= 4; count++) {
        akn_lab_beeps b;
        akn_lab_beeps_init(&b, 48000, count, 0.25);
        static int16_t buf[2 * 1024];
        unsigned long frames = 0, audible = 0;
        unsigned onsets = 0;
        int was_on = 0;
        int16_t peak = 0;
        while (!akn_lab_beeps_done(&b)) {
            audible += akn_lab_beeps_fill(&b, buf, 1024);
            for (int i = 0; i < 1024; i++) {
                int on = buf[2 * i] != 0 || buf[2 * i + 1] != 0;
                if (on && !was_on) onsets++;
                was_on = on;
                CHECK(buf[2 * i] == buf[2 * i + 1]);
                if (abs(buf[2 * i]) > peak) peak = (int16_t)abs(buf[2 * i]);
            }
            frames += 1024;
            CHECK(frames < 48000u * 3);
            if (frames >= 48000u * 3) break;
        }
        /* each beep is 150 ms; zero crossings may split a beep into several nonzero runs, so count by audible time */
        CHECK(audible == count * 7200u);
        CHECK(onsets >= count);
        CHECK(peak > 8000 && peak <= 8192);
    }
    akn_lab_beeps b;
    akn_lab_beeps_init(&b, 48000, 2, 3.0);
    CHECK(b.amplitude == 0.5); /* clamped: the lab never plays louder than -6 dBFS */
}

static void test_ring(void) {
    akn_lab_ring r;
    CHECK(akn_lab_ring_init(&r, 8) == 0);
    int16_t in[12], out[12];
    for (int i = 0; i < 12; i++) in[i] = (int16_t)(i + 1);
    akn_lab_ring_push(&r, in, 5);
    akn_lab_ring_pop(&r, out, 3);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);
    akn_lab_ring_push(&r, in + 5, 7); /* 2 + 7 = 9 > 8: the oldest (4) is dropped */
    CHECK(r.dropped == 1);
    akn_lab_ring_pop(&r, out, 10);
    CHECK(out[0] == 5 && out[7] == 12);
    CHECK(out[8] == 0 && out[9] == 0);
    CHECK(r.underrun == 2);
    akn_lab_ring_free(&r);

    int16_t mono[2] = { 1000, -32768 }, stereo[12];
    akn_lab_upsample3_stereo(mono, 2, stereo, 0.5);
    for (int i = 0; i < 6; i++) CHECK(stereo[i] == 500);
    for (int i = 6; i < 12; i++) CHECK(stereo[i] == -16384);
    akn_lab_upsample3_stereo(mono, 1, stereo, 40.0);
    CHECK(stereo[0] == 32767); /* clipped, not wrapped */
}

static void test_settings(void) {
    char path[] = "/tmp/akn-lab-settings-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd < 0) return;
    char host[64];
    int port = 0;
    const char *cases[][3] = {
        { "{\"server\":\"https://akeno.example\"}", "akeno.example", "443" },
        { "{\"server\":\"http://192.168.1.5:8080/x\"}", "192.168.1.5", "8080" },
        { "{\"server\":\"https://h.example:99999\"}", "h.example", "443" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        CHECK(ftruncate(fd, 0) == 0);
        CHECK(pwrite(fd, cases[i][0], strlen(cases[i][0]), 0) == (ssize_t)strlen(cases[i][0]));
        CHECK(akn_lab_server_from_settings(path, host, sizeof(host), &port) == 0);
        CHECK(strcmp(host, cases[i][1]) == 0);
        CHECK(port == atoi(cases[i][2]));
    }
    const char *bad[] = { "{\"server\":\"ftp://x\"}", "{}", "not json", "{\"server\":\"https://\"}" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(ftruncate(fd, 0) == 0);
        CHECK(pwrite(fd, bad[i], strlen(bad[i]), 0) == (ssize_t)strlen(bad[i]));
        CHECK(akn_lab_server_from_settings(path, host, sizeof(host), &port) == -1);
    }
    close(fd);
    unlink(path);
    CHECK(akn_lab_server_from_settings("/nonexistent/akeno/settings.json", host, sizeof(host), &port) == -1);
}

static void test_probe(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t al = sizeof(a);
    CHECK(bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0 && listen(fd, 4) == 0);
    CHECK(getsockname(fd, (struct sockaddr *)&a, &al) == 0);
    int port = ntohs(a.sin_port), ms = -1;
    CHECK(akn_lab_probe_tcp("127.0.0.1", port, 1000, &ms) == 0);
    CHECK(ms >= 0);
    close(fd);
    /* nothing listens there any more */
    CHECK(akn_lab_probe_tcp("127.0.0.1", port, 1000, &ms) != 0);
    CHECK(akn_lab_probe_http("127.0.0.1", port, "/api/instance", 1000, &ms) != 0);
}

static akn_lab_run g_run;
static char g_report[32768];

static void run_sim(akn_lab_sim *sim, const akn_lab_options *opt) {
    akn_lab_backend be;
    akn_lab_backend_host(&be, sim);
    akn_lab_execute(&be, opt, &g_run);
    akn_lab_report(&g_run, g_report, sizeof(g_report));
}

static void test_runs(void) {
    akn_lab_options opt;
    memset(&opt, 0, sizeof(opt));
    opt.quick = 1;
    opt.wait_game_s = 1;

    /* speech in both phases; every port opens */
    akn_lab_sim sim;
    memset(&sim, 0, sizeof(sim));
    sim.game_after_polls = 1;
    sim.pace = 1;
    run_sim(&sim, &opt);
    CHECK(g_run.game_seen);
    CHECK(g_run.ran[AKN_PHASE_HOME] && g_run.ran[AKN_PHASE_GAME]);
    CHECK(g_run.mic[AKN_PHASE_HOME].verdict == AKN_LAB_PCM_SOUND);
    CHECK(g_run.mic[AKN_PHASE_GAME].verdict == AKN_LAB_PCM_SOUND);
    CHECK(g_run.mic[AKN_PHASE_GAME].stats.samples == 20 * 256); /* quick mode: 20 reads */
    for (int i = 0; i < AKN_LAB_TONES; i++) {
        CHECK(g_run.tones[AKN_PHASE_GAME][i].open_rc == 0);
        CHECK(g_run.tones[AKN_PHASE_GAME][i].beeps == (unsigned)i + 1);
        CHECK(g_run.tones[AKN_PHASE_GAME][i].ok == g_run.tones[AKN_PHASE_GAME][i].calls);
        CHECK(g_run.tones[AKN_PHASE_GAME][i].paced);
    }
    CHECK(g_run.echo.out_open_rc == 0 && g_run.echo.out_calls > 0);
    CHECK(g_run.echo.mic.verdict == AKN_LAB_PCM_SOUND);
    CHECK(g_run.net[AKN_PHASE_GAME].beats > 0);
    CHECK(g_run.net[AKN_PHASE_HOME].daemon_ok > 0);
    CHECK(strstr(g_report, "A.home.mic=SOUND") != NULL);
    CHECK(strstr(g_report, "A.game.mic=SOUND") != NULL);
    CHECK(strstr(g_report, "B.game.tone4=ACCEPTED_PACED") != NULL);
    CHECK(strstr(g_report, "C.game.echo=RAN") != NULL);
    CHECK(strstr(g_report, "heard=____") != NULL); /* hearing is the player's answer, never assumed */
    CHECK(strstr(g_report, "E.game_audio_still_heard=____") != NULL);
    CHECK(strstr(g_report, "game_seen=yes") != NULL);
    CHECK(strstr(g_report, "PC simulation") != NULL);
    CHECK(sim.notifications >= 6);
    CHECK(strstr(sim.last_notification, "finished") != NULL);

    /* a muted microphone: the API works but delivers zeros, which must not read as sound */
    memset(&sim, 0, sizeof(sim));
    sim.mic_mode = 1;
    sim.game_after_polls = 1;
    run_sim(&sim, &opt);
    CHECK(g_run.mic[AKN_PHASE_GAME].open_rc == 0);
    CHECK(g_run.mic[AKN_PHASE_GAME].read_errors == 0);
    CHECK(g_run.mic[AKN_PHASE_GAME].verdict == AKN_LAB_PCM_ALL_ZERO);
    CHECK(strstr(g_report, "A.game.mic=ALL_ZERO") != NULL);
    CHECK(strstr(g_report, "A.game.mic=SOUND") == NULL);
    CHECK(strstr(g_report, "B.game.tone1=ACCEPTED_NOT_PACED") != NULL);

    /* a stuck value */
    memset(&sim, 0, sizeof(sim));
    sim.mic_mode = 3;
    sim.game_after_polls = 1;
    run_sim(&sim, &opt);
    CHECK(g_run.mic[AKN_PHASE_HOME].verdict == AKN_LAB_PCM_CONSTANT);

    /* the microphone and the voice/personal ports are refused */
    memset(&sim, 0, sizeof(sim));
    sim.mic_mode = 2;
    sim.fail_port_types = (1 << 2) | (1 << 3);
    sim.game_after_polls = 1;
    run_sim(&sim, &opt);
    CHECK(g_run.mic[AKN_PHASE_HOME].open_rc != 0);
    CHECK(strstr(g_report, "A.home.mic=OPEN_FAILED code=0x80260105") != NULL);
    CHECK(strstr(g_report, "B.game.tone3=OPEN_FAILED") != NULL);
    CHECK(strstr(g_report, "B.game.tone4=OPEN_FAILED") != NULL);
    CHECK(strstr(g_report, "B.game.tone2=ACCEPTED") != NULL);
    CHECK(strstr(g_report, "C.game.echo=NOT_RUN") != NULL);

    /* no foreground user: the user variants cannot run but the system-user one does */
    memset(&sim, 0, sizeof(sim));
    sim.no_foreground_user = 1;
    sim.game_after_polls = 1;
    run_sim(&sim, &opt);
    CHECK(g_run.tones[AKN_PHASE_HOME][0].open_rc == 0);
    CHECK(g_run.tones[AKN_PHASE_HOME][1].open_rc != 0);
    CHECK(g_run.tones[AKN_PHASE_HOME][1].calls == 0);

    /* nobody starts a game: only the home phase, and the report says so */
    memset(&sim, 0, sizeof(sim));
    sim.game_after_polls = -1;
    run_sim(&sim, &opt);
    CHECK(!g_run.game_seen);
    CHECK(g_run.ran[AKN_PHASE_HOME] && !g_run.ran[AKN_PHASE_GAME]);
    CHECK(strstr(g_report, "A.game.mic=NOT_RUN") != NULL);
    CHECK(strstr(g_report, "game_seen=no") != NULL);
    CHECK(strstr(g_report, "game tests skipped") != NULL);

    /* a game is already running: skip_home goes straight to the game phase */
    memset(&sim, 0, sizeof(sim));
    sim.game_after_polls = 0;
    akn_lab_options skip = opt;
    skip.skip_home = 1;
    skip.no_echo = 1;
    run_sim(&sim, &skip);
    CHECK(!g_run.ran[AKN_PHASE_HOME] && g_run.ran[AKN_PHASE_GAME]);
    CHECK(strstr(g_report, "C.game.echo=NOT_RUN") != NULL);

    /* the report fits and is bounded */
    char tiny[64];
    size_t n = akn_lab_report(&g_run, tiny, sizeof(tiny));
    CHECK(n < sizeof(tiny) && strlen(tiny) == n);
}

int main(void) {
    test_stats();
    test_beeps();
    test_ring();
    test_settings();
    test_probe();
    test_runs();
    printf("voicelab: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
