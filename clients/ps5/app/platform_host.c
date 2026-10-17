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
#include <time.h>
#ifdef AKENO_EXPERIMENTAL_AUDIO
#include <stdint.h>
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

/*
 * Simulated controller for the shortcut tests: AKENO_SIM_PAD names a file holding the pressed
 * buttons as a number ("0x0c08"), re-read on every read; "fail" simulates a read error and
 * "disconnected" a controller that is not connected; a missing file means nothing is pressed.
 * AKENO_SIM_PAD_OPEN=fail makes opening fail. Without AKENO_SIM_PAD the function is unavailable,
 * as on a console where libScePad.sprx cannot be loaded.
 */
static int g_sim_pad_open = 0;

int akeno_platform_pad_open(akeno_pad_info *info, char *err, size_t err_len) {
    memset(info, 0, sizeof(*info));
    info->user_id = -1;
    info->handle = -1;
    snprintf(info->method, sizeof(info->method), "simulated controller (AKENO_SIM_PAD, host build)");
    const char *path = getenv("AKENO_SIM_PAD");
    if (!path || !path[0]) {
        snprintf(err, err_len, "Controller input is only available on the PS5 (host build).");
        return -2;
    }
    info->library_loaded = 1;
    info->init_called = 1;
    const char *mode = getenv("AKENO_SIM_PAD_OPEN");
    if (mode && strcmp(mode, "fail") == 0) {
        info->user_id = 0x10000001;
        snprintf(info->user_source, sizeof(info->user_source), "foreground user");
        info->open_rc = (int)0x80920001u;
        snprintf(err, err_len, "scePadOpen returned 0x%08x (simulated)", (unsigned)info->open_rc);
        return -1;
    }
    info->user_id = 0x10000001;
    snprintf(info->user_source, sizeof(info->user_source), "foreground user");
    info->open_rc = info->handle = 1;
    g_sim_pad_open = 1;
    return 0;
}

int akeno_platform_pad_read(unsigned *buttons, int *connected, int *rc) {
    *buttons = 0;
    *connected = 0;
    *rc = 0;
    const char *path = getenv("AKENO_SIM_PAD");
    if (!g_sim_pad_open || !path) {
        *rc = -1;
        return -1;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        *connected = 1;
        return 0;
    }
    char line[64] = "";
    if (!fgets(line, sizeof(line), f)) line[0] = '\0';
    fclose(f);
    if (strncmp(line, "fail", 4) == 0) {
        *rc = (int)0x80920003u;
        return -1;
    }
    if (strncmp(line, "disconnected", 12) == 0) return 0;
    *connected = 1;
    *buttons = (unsigned)strtoul(line, NULL, 0);
    return 0;
}

void akeno_platform_pad_close(void) { g_sim_pad_open = 0; }

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

/*
 * AKENO_SIM_AUDIO_FAIL=init|open|output|close makes that step fail with a PS4-style error code;
 * AKENO_SIM_AUDIO_PACE=none makes output calls return at once (samples "accepted" faster than
 * real time) instead of blocking for one block's duration as the real function does.
 */
#include "audio_lab_pcm.h"

static int sim_audio_fail(const char *step) {
    const char *fail = getenv("AKENO_SIM_AUDIO_FAIL");
    return fail && strcmp(fail, step) == 0;
}

static long long sim_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

static int g_sim_audio_init_done = 0;

int akeno_platform_audio_run(const akeno_audio_params *p, akeno_audio_result *r) {
    static unsigned char block[AUDIO_LAB_MAX_BLOCK_BYTES] __attribute__((aligned(64)));
    memset(r, 0, sizeof(*r));
    unsigned frame_bytes = audio_lab_format_frame_bytes(p->format);
    if (!frame_bytes || !audio_lab_grain_valid(p->grain) || p->rate != AUDIO_LAB_RATE || p->user_id != 0xFF || p->port_type != 0) {
        snprintf(r->error, sizeof(r->error), "Parameters outside the verified SDL driver configuration");
        return -1;
    }
    r->buffer_aligned = ((uintptr_t)block & 63u) == 0;
    if (!sim_audio_has("out")) {
        snprintf(r->error, sizeof(r->error), "dlopen(libSceAudioOut.sprx) failed: not available (host build)");
        return -2;
    }
    r->library_loaded = 1;
    if (!g_sim_audio_init_done || sim_audio_fail("init")) {
        r->init_called = 1;
        r->init_rc = sim_audio_fail("init") ? (int)0x80260001u : 0;
        g_sim_audio_init_done = r->init_rc == 0;
    }
    if (sim_audio_fail("init")) {
        snprintf(r->error, sizeof(r->error), "sceAudioOutInit returned 0x%08x", (unsigned)r->init_rc);
        return -1;
    }
    r->open_called = 1;
    r->open_rc = sim_audio_fail("open") ? (int)0x80260005u : 17;
    if (r->open_rc < 1) {
        snprintf(r->error, sizeof(r->error), "sceAudioOutOpen returned 0x%08x", (unsigned)r->open_rc);
        return -1;
    }
    const char *pace = getenv("AKENO_SIM_AUDIO_PACE");
    int realtime = !pace || strcmp(pace, "none") != 0;
    long long block_us = (long long)p->grain * 1000000LL / (long long)p->rate;
    long long first = 0, last = 0;
    int result = 0;
    r->call_min_us = -1;
    for (unsigned long done = 0; done < p->frames; done += p->grain) {
        if (p->cancel && *p->cancel) {
            r->cancelled = 1;
            break;
        }
        double peak = 0.0;
        unsigned long nonzero = p->fill(p->fill_user, block, p->grain, &peak);
        long long t0 = sim_now_us();
        if (!first) first = t0;
        /* Like the real function: the first block is queued at once, later ones wait for room. */
        if (realtime && r->calls > 0) {
            struct timespec ts = { (time_t)(block_us / 1000000LL), (long)(block_us % 1000000LL) * 1000L };
            nanosleep(&ts, NULL);
        }
        long long t1 = sim_now_us();
        last = t1;
        r->calls++;
        int rc = sim_audio_fail("output") && r->calls == 3 ? (int)0x80260007u : 0;
        r->last_output_rc = rc;
        if (rc < 0) {
            r->first_error_rc = rc;
            r->first_error_call = r->calls;
            snprintf(r->error, sizeof(r->error), "sceAudioOutOutput returned 0x%08x on call %lu", (unsigned)rc, r->calls);
            result = -1;
            break;
        }
        r->calls_ok++;
        r->frames_submitted += p->grain;
        r->nonzero_frames += nonzero;
        if (peak > r->peak) r->peak = peak;
        if (r->calls > 1) {
            long long us = t1 - t0;
            r->call_total_us += us;
            if (r->call_min_us < 0 || us < r->call_min_us) r->call_min_us = us;
            if (us > r->call_max_us) r->call_max_us = us;
        }
    }
    r->wall_us = first ? last - first : 0;
    r->close_called = 1;
    r->close_rc = sim_audio_fail("close") ? (int)0x80260006u : 0;
    if (r->close_rc != 0 && result == 0) {
        snprintf(r->error, sizeof(r->error), "sceAudioOutClose returned 0x%08x", (unsigned)r->close_rc);
        result = -1;
    }
    if (r->call_min_us < 0) r->call_min_us = 0;
    pthread_mutex_lock(&g_out);
    fprintf(stdout, "{\"event\":\"audio-run\",\"format\":%d,\"grain\":%u,\"frames\":%lu,\"calls\":%lu,\"nonzero\":%lu,\"peak\":%.4f,\"cancelled\":%s}\n",
            p->format, p->grain, r->frames_submitted, r->calls, r->nonzero_frames, r->peak, r->cancelled ? "true" : "false");
    fflush(stdout);
    pthread_mutex_unlock(&g_out);
    return result;
}
#endif
