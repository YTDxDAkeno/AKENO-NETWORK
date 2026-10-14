/*
 * Audio lab (AKENO_EXPERIMENTAL_AUDIO builds only). See audio_lab.h and
 * docs/PS5_AUDIO_RESEARCH.md.
 *
 * Prototype A cannot enumerate microphones: the pinned ps5-payload-sdk has no libSceAudioIn
 * interface and no verified PS5 declaration of one exists, so calling it would mean guessing a
 * function signature. What can be established without guessing is reported instead: which
 * audio-related libraries the system has and whether they export the functions a voice client
 * would need (looked up by name, never called).
 *
 * Prototype C plays a one-second, quiet 440 Hz tone through sceAudioOut, using the declarations
 * of the PS5 audio driver of ps5-payload-dev/SDL (see platform_ps5.c).
 */
#define _POSIX_C_SOURCE 200809L
#include "audio_lab.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "platform.h"

#define TONE_FREQUENCY_HZ 440u
#define TONE_DURATION_MS 1000u
#define TONE_AMPLITUDE 0.1 /* -20 dBFS: clearly audible, not loud */

typedef struct {
    const char *library;
    const char *names[4];
    size_t count;
    const char *source;
} probe_library;

/*
 * libSceAudioOut: names from the SDK's stub (sce_stubs/libSceAudioOut.c) and the SDL driver.
 * libSceAudioIn: not in the SDK; the names are those of the PS4's capture interface (public PS4
 * homebrew stubs). Finding them on a PS5 says nothing about their PS5 signatures.
 */
static const probe_library k_libraries[] = {
    { "libSceAudioOut.sprx", { "sceAudioOutInit", "sceAudioOutOpen", "sceAudioOutOutput", "sceAudioOutClose" }, 4,
      "ps5-payload-sdk stub + ps5-payload-dev/SDL" },
    { "libSceAudioIn.sprx", { "sceAudioInOpen", "sceAudioInInput", "sceAudioInClose" }, 3, "PS4 homebrew stubs (not in the PS5 SDK)" },
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char *g_probe_json = NULL; /* g_lock: result of the last probe */
static pthread_t g_tone_thread;
static int g_tone_thread_started = 0; /* main thread only */
static int g_tone_running = 0;        /* g_lock */
static int g_tone_runs = 0;           /* g_lock */
static int g_tone_result = 1;         /* g_lock: 1 none yet, 0 ok, -1 failed, -2 unavailable */
static char g_tone_detail[200] = "";  /* g_lock */

static long long now_seconds(void) { return (long long)time(NULL); }

/*
 * Library files looked for (stat only). A fixed list instead of a directory listing: the audio
 * lab must not add a single import to the console app that has not been proven on a console
 * (console/reviewed-imports.txt), so that it starts exactly like the release build.
 */
static const char *const k_candidate_files[] = {
    "libSceAudioOut.sprx", "libSceAudioIn.sprx", "libSceAudio3d.sprx", "libSceAudiodec.sprx", "libSceVoice.sprx", "libSceVoiceQoS.sprx",
};

/* Reports which candidate library files exist in the system library directory (read-only). */
static void probe_library_dir(akeno_buf *b) {
    const char *dir = akeno_platform_audio_library_dir();
    akeno_buf_append_str(b, "\"libraryDir\":");
    akeno_buf_append_json_string(b, dir ? dir : "");
    struct stat st;
    if (!dir || stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        akeno_buf_printf(b, ",\"libraryDirReadable\":false,\"libraryDirError\":\"%s\",\"audioFiles\":[]",
                         dir ? "not found or not a directory" : "unknown on this platform");
        return;
    }
    akeno_buf_append_str(b, ",\"libraryDirReadable\":true,\"audioFiles\":[");
    int first = 1;
    for (size_t i = 0; i < sizeof(k_candidate_files) / sizeof(k_candidate_files[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, k_candidate_files[i]);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (!first) akeno_buf_append_str(b, ",");
        akeno_buf_append_json_string(b, k_candidate_files[i]);
        first = 0;
    }
    akeno_buf_printf(b, "],\"filesChecked\":%zu", sizeof(k_candidate_files) / sizeof(k_candidate_files[0]));
}

void audio_lab_probe(void) {
    akeno_buf b;
    akeno_buf_init(&b, 64 * 1024);
    akeno_buf_printf(&b, "{\"ranAt\":%lld,", now_seconds());
    probe_library_dir(&b);
    akeno_buf_append_str(&b, ",\"libraries\":[");
    for (size_t l = 0; l < sizeof(k_libraries) / sizeof(k_libraries[0]); l++) {
        const probe_library *lib = &k_libraries[l];
        int found[4] = { 0 };
        char err[200] = "";
        int loaded = akeno_platform_audio_lookup(lib->library, lib->names, found, lib->count, err, sizeof(err)) == 0;
        if (l) akeno_buf_append_str(&b, ",");
        akeno_buf_append_str(&b, "{\"name\":");
        akeno_buf_append_json_string(&b, lib->library);
        akeno_buf_append_str(&b, ",\"namesFrom\":");
        akeno_buf_append_json_string(&b, lib->source);
        akeno_buf_printf(&b, ",\"loaded\":%s,\"error\":", loaded ? "true" : "false");
        akeno_buf_append_json_string(&b, loaded ? "" : err);
        akeno_buf_append_str(&b, ",\"symbols\":[");
        for (size_t i = 0; i < lib->count; i++) {
            if (i) akeno_buf_append_str(&b, ",");
            akeno_buf_append_str(&b, "{\"name\":");
            akeno_buf_append_json_string(&b, lib->names[i]);
            akeno_buf_printf(&b, ",\"exported\":%s}", found[i] ? "true" : "false");
        }
        akeno_buf_append_str(&b, "]}");
    }
    akeno_buf_append_str(&b, "],\"microphoneEnumeration\":");
    akeno_buf_append_json_string(&b,
                                 "not attempted: the PS5 SDK has no microphone (libSceAudioIn) interface and no verified "
                                 "PS5 declaration of it exists; calling it would mean guessing a signature");
    akeno_buf_append_str(&b, "}");

    pthread_mutex_lock(&g_lock);
    free(g_probe_json);
    g_probe_json = b.data ? strdup(b.data) : NULL;
    pthread_mutex_unlock(&g_lock);
    akeno_buf_free(&b);
}

static void *tone_worker(void *arg) {
    (void)arg;
    char err[200] = "";
    int rc = akeno_platform_audio_tone(TONE_FREQUENCY_HZ, TONE_DURATION_MS, TONE_AMPLITUDE, err, sizeof(err));
    pthread_mutex_lock(&g_lock);
    g_tone_result = rc;
    snprintf(g_tone_detail, sizeof(g_tone_detail), "%s",
             rc == 0 ? "The system accepted the audio. Did you hear a short tone? Please report where (TV, headset)." : err);
    g_tone_running = 0;
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

int audio_lab_tone_start(void) {
    pthread_mutex_lock(&g_lock);
    if (g_tone_running) {
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    pthread_mutex_unlock(&g_lock);
    /* The previous worker has finished (not running); collect it before starting another. */
    if (g_tone_thread_started) {
        pthread_join(g_tone_thread, NULL);
        g_tone_thread_started = 0;
    }
    pthread_mutex_lock(&g_lock);
    g_tone_running = 1;
    g_tone_runs++;
    snprintf(g_tone_detail, sizeof(g_tone_detail), "playing");
    pthread_mutex_unlock(&g_lock);
    if (pthread_create(&g_tone_thread, NULL, tone_worker, NULL) != 0) {
        pthread_mutex_lock(&g_lock);
        g_tone_running = 0;
        g_tone_result = -1;
        snprintf(g_tone_detail, sizeof(g_tone_detail), "could not start a thread");
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    g_tone_thread_started = 1;
    return 0;
}

void audio_lab_status_json(akeno_buf *b) {
    pthread_mutex_lock(&g_lock);
    akeno_buf_append_str(b, "{\"experimental\":true,\"probe\":");
    akeno_buf_append_str(b, g_probe_json ? g_probe_json : "null");
    const char *result = g_tone_running ? "playing"
                         : g_tone_result == 1 ? "none"
                         : g_tone_result == 0 ? "accepted"
                         : g_tone_result == -2 ? "unavailable"
                                               : "failed";
    akeno_buf_printf(b, ",\"tone\":{\"frequencyHz\":%u,\"durationMs\":%u,\"runs\":%d,\"result\":\"%s\",\"detail\":", TONE_FREQUENCY_HZ,
                     TONE_DURATION_MS, g_tone_runs, result);
    akeno_buf_append_json_string(b, g_tone_detail);
    akeno_buf_append_str(b, "}}");
    pthread_mutex_unlock(&g_lock);
}

void audio_lab_shutdown(void) {
    if (g_tone_thread_started) pthread_join(g_tone_thread, NULL);
    g_tone_thread_started = 0;
    pthread_mutex_lock(&g_lock);
    free(g_probe_json);
    g_probe_json = NULL;
    pthread_mutex_unlock(&g_lock);
}
