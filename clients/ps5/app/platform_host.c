/*
 * Host (Linux) platform layer for running and testing the console app on a PC. Notifications and
 * browser requests are written to stdout as JSON lines so the test harness can observe them.
 *
 * Game detection is simulated: AKENO_SIM_PROCESSES names a file with one process per line,
 * "<pid> <title-id or -> [running|stopped] [ps|alt]", re-read on every scan (a missing file
 * means no processes, a line "fail" a listing error). Each title ID is written into a zeroed raw
 * application-info buffer at the offset of the line's layout, else of AKENO_SIM_APPINFO_LAYOUT
 * ("ps" = 20, the default; "alt" = 16; "mixed" = alternating, i.e. an inconsistent console),
 * and decoded with the same parser as on the PS5. AKENO_SIM_METADATA_ROOT stands in for the console's root when
 * title names are looked up. Without these variables the host build reports no game detection.
 */
#define _POSIX_C_SOURCE 200809L
#include "platform.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef AKENO_EXPERIMENTAL_AUDIO
#include <time.h>
#endif

static pthread_mutex_t g_out = PTHREAD_MUTEX_INITIALIZER;

static void print_json_string(const char *s) {
    fputc('"', stdout);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(stdout, "\\%c", *p);
        else if (*p < 0x20) fprintf(stdout, "\\u%04x", *p);
        else fputc(*p, stdout);
    }
    fputc('"', stdout);
}

void akeno_platform_init(akeno_platform_info *info) {
    memset(info, 0, sizeof(*info));
#if defined(__linux__)
    info->name = "Linux";
#else
    info->name = "Host";
#endif
    info->notifications = "printed to standard output (host build)";
    info->browser = "URL printed to standard output (host build)";
    const char *sim = getenv("AKENO_SIM_PROCESSES");
    info->game_detection = sim && sim[0] ? "simulated process list (AKENO_SIM_PROCESSES)" : "not available on this platform";
    info->actionable = "printed to standard output (host build)";
}

void akeno_platform_cleanup(void) {}

int akeno_platform_notify(const char *text) {
    pthread_mutex_lock(&g_out);
    fputs("{\"event\":\"notification\",\"text\":", stdout);
    print_json_string(text);
    fputs("}\n", stdout);
    fflush(stdout);
    pthread_mutex_unlock(&g_out);
    return 0;
}

int akeno_platform_open_browser(const char *url) {
    /* Test switch: simulate a console where the browser cannot be launched. */
    const char *fail = getenv("AKENO_TEST_BROWSER_FAILS");
    int failed = fail && fail[0] == '1';
    pthread_mutex_lock(&g_out);
    fputs("{\"event\":\"open-browser\",\"url\":", stdout);
    print_json_string(url);
    fputs(failed ? ",\"failed\":true}\n" : "}\n", stdout);
    fflush(stdout);
    pthread_mutex_unlock(&g_out);
    return failed ? -1 : 0;
}

/* Test switch AKENO_TEST_ACTIONABLE: "unavailable" (-2) or "refused" (-1) simulate a console
 * where the button notification cannot be used, so the fallback can be tested. */
static const char *actionable_mode(void) {
    const char *mode = getenv("AKENO_TEST_ACTIONABLE");
    return mode ? mode : "";
}

const char *akeno_platform_actionable_status(void) {
    const char *mode = actionable_mode();
    if (strcmp(mode, "unavailable") == 0) return "not available (simulated)";
    if (strcmp(mode, "refused") == 0) return "loaded; the system refuses (simulated)";
    return "printed to standard output (host build)";
}

int akeno_platform_notify_action(const char *title, const char *text, const char *button, const char *url) {
    const char *mode = actionable_mode();
    if (strcmp(mode, "unavailable") == 0) return -2;
    if (strcmp(mode, "refused") == 0) return -1;
    pthread_mutex_lock(&g_out);
    fputs("{\"event\":\"notification\",\"text\":", stdout);
    print_json_string(title);
    fputs(",\"sub\":", stdout);
    print_json_string(text);
    fputs(",\"action\":{\"label\":", stdout);
    print_json_string(button);
    fputs(",\"url\":", stdout);
    print_json_string(url);
    fputs("}}\n", stdout);
    fflush(stdout);
    pthread_mutex_unlock(&g_out);
    return 0;
}

int akeno_platform_list_apps(akeno_proc_info *out, size_t max, size_t *total, char *err, size_t err_len) {
    *total = 0;
    const char *path = getenv("AKENO_SIM_PROCESSES");
    if (!path || !path[0]) {
        snprintf(err, err_len, "Game detection is only available on the PS5.");
        return -1;
    }
    const char *layout = getenv("AKENO_SIM_APPINFO_LAYOUT");
    int mode = !layout || strcmp(layout, "alt") != 0 ? (layout && strcmp(layout, "mixed") == 0 ? 2 : 0) : 1;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    size_t n = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "fail", 4) == 0) {
            fclose(f);
            snprintf(err, err_len, "Simulated process listing failure.");
            return -1;
        }
        int pid = 0;
        char title[32] = "", state[16] = "", layout_override[8] = "";
        if (line[0] == '#' || sscanf(line, "%d %31s %15s %7s", &pid, title, state, layout_override) < 2) continue;
        (*total)++;
        if (n >= max) continue;
        akeno_proc_info *o = &out[n++];
        memset(o, 0, sizeof(*o));
        o->pid = pid;
        o->state = strcmp(state, "stopped") == 0 ? AKENO_PROC_STOPPED : AKENO_PROC_ACTIVE;
        o->has_app_info = strcmp(title, "-") != 0;
        snprintf(o->comm, sizeof(o->comm), "sim%d", pid);
        if (!o->has_app_info) continue;
        /* What sceKernelGetAppInfo would have written, with the title ID at the simulated offset. */
        unsigned char raw[128];
        memset(raw, 0, sizeof(raw));
        raw[0] = (unsigned char)(pid & 0xFF);
        raw[1] = (unsigned char)((pid >> 8) & 0xFF);
        int alt = mode == 1 || (mode == 2 && (n % 2) == 0);
        if (strcmp(layout_override, "alt") == 0) alt = 1;
        else if (strcmp(layout_override, "ps") == 0) alt = 0;
        size_t offset = alt ? AKENO_APPINFO_OFFSET_TITLE_ALT : AKENO_APPINFO_OFFSET_TITLE_PS;
        size_t len = strnlen(title, sizeof(title));
        if (len > 13) len = 13; /* The title field is at most 14 bytes in either layout. */
        memcpy(raw + offset, title, len);
        akeno_appinfo_parse(raw, sizeof(raw), o);
    }
    fclose(f);
    return (int)n;
}

const char *akeno_platform_metadata_root(void) {
    const char *root = getenv("AKENO_SIM_METADATA_ROOT");
    return root && root[0] ? root : NULL;
}

void akeno_platform_debug_line(const char *line) {
    fprintf(stderr, "%s\n", line);
}

#ifdef AKENO_EXPERIMENTAL_AUDIO
/*
 * Simulated audio for testing the audio lab on a PC: AKENO_SIM_AUDIO_LIBDIR stands in for the
 * console's library directory; AKENO_SIM_AUDIO lists the libraries that "load" and export every
 * name asked for ("out", "in", "out,in"); a tone is printed as a JSON line instead of played.
 */

static int sim_audio_has(const char *what) {
    const char *sim = getenv("AKENO_SIM_AUDIO");
    return sim && strstr(sim, what) != NULL;
}

const char *akeno_platform_audio_library_dir(void) {
    const char *dir = getenv("AKENO_SIM_AUDIO_LIBDIR");
    return dir && dir[0] ? dir : NULL;
}

int akeno_platform_audio_lookup(const char *library, const char *const *names, int *found, size_t count, char *err, size_t err_len) {
    (void)names;
    int loads = (strstr(library, "AudioOut") && sim_audio_has("out")) || (strstr(library, "AudioIn") && sim_audio_has("in"));
    for (size_t i = 0; i < count; i++) found[i] = loads;
    if (!loads) {
        snprintf(err, err_len, "dlopen failed: %s is not available (host build)", library);
        return -1;
    }
    return 0;
}

int akeno_platform_audio_tone(unsigned frequency_hz, unsigned duration_ms, double amplitude, char *err, size_t err_len) {
    if (!sim_audio_has("out")) {
        snprintf(err, err_len, "dlopen(libSceAudioOut.sprx) failed: not available (host build)");
        return -2;
    }
    struct timespec ts = { (time_t)(duration_ms / 1000), (long)(duration_ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
    pthread_mutex_lock(&g_out);
    fprintf(stdout, "{\"event\":\"audio-tone\",\"frequency\":%u,\"durationMs\":%u,\"amplitude\":%.3f}\n", frequency_hz, duration_ms, amplitude);
    fflush(stdout);
    pthread_mutex_unlock(&g_out);
    return 0;
}
#endif
