/*
 * AKENO Audio Lab 3.1 - PS5 backend. The ONLY file that declares or calls console functions. Every
 * audio call and its parameters are taken unchanged from the proven Audio Lab v3
 * (clients/ps5/app/platform_ps5.c at commit 05ad4f3), which ran on firmware 12.20:
 *
 *   sceKernelSendNotificationRequest   samples/hello_world (12.20)        - start/tone/stop notices
 *   kernel_get_fw_version              include/ps5/kernel.h               - read-only firmware
 *   dlopen/dlsym/dlclose               samples/hello_dlfcn                - libSceAudioOut.sprx only
 *   sceAudioOutInit/Open/Output/Close  ps5-payload-dev/SDL src/audio/ps5  - the test's own port
 *   sceAudioOutGetPortState            OpenOrbis/shadPS4/fpPS4/SharpProspero/AnyPS5 - read-only, on
 *                                      the test's OWN handle, ONLY when the owner opted in; guarded
 *                                      arena (audio_lab_state.*), permanent stop on any overrun
 *   flock/open/read/stat/rename        POSIX - single instance, control file, reports
 *
 * Not used, by rule (console/check-audiolab.sh): no microphone, no socket, no app launch/kill,
 * mount, ptrace, kernel memory helper, pad, registry; and no volume/routing/focus/attach/HDMI
 * audio function (sceAudioOutSetVolume, ...GetFocusEnablePid, ...AttachToApplicationByPid, ...).
 */
#include "al_backend.h"
#include "al.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

typedef struct {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int blocking);

typedef int32_t (*audio_out_init_fn)(void);
typedef int32_t (*audio_out_open_fn)(int32_t user_id, int32_t type, int32_t index, uint32_t len, uint32_t freq, uint32_t param);
typedef int32_t (*audio_out_output_fn)(int32_t handle, const void *samples);
typedef int32_t (*audio_out_close_fn)(int32_t handle);

static void *g_audio_lib;
static audio_out_open_fn g_open;
static audio_out_output_fn g_output;
static audio_out_close_fn g_close;
static int g_init_done, g_init_rc;
static audio_lab_port_state_fn g_get_port_state; /* NULL when not exported */
static int g_port_state_stopped;                 /* wrote outside its window once: never again */
static unsigned char g_block[AUDIO_LAB_MAX_BLOCK_BYTES] __attribute__((aligned(64)));
static unsigned char g_arena[AUDIO_LAB_STATE_ARENA_BYTES] __attribute__((aligned(64)));
static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;

static long long p_now_unix(void *u) { (void)u; return (long long)time(NULL); }

static uint64_t p_now_us(void *u) {
    (void)u;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void p_sleep_ms(void *u, unsigned ms) {
    (void)u;
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int p_notify(void *u, const char *text) {
    (void)u;
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.message, sizeof(req.message), "%s", text);
    return sceKernelSendNotificationRequest(0, &req, sizeof(req), 0) == 0 ? 0 : -1;
}

static int p_fw(void *u, uint32_t *raw) {
    (void)u;
    uint32_t v = kernel_get_fw_version();
    *raw = v;
    return v != 0;
}

static int p_pid(void *u) { (void)u; return (int)getpid(); }

/* g_audio_lock held. Loads libSceAudioOut once and runs sceAudioOutInit once. */
static int audio_load(al_audio_result *r) {
    if (!g_audio_lib) {
        void *lib = dlopen("libSceAudioOut.sprx", RTLD_LAZY);
        if (!lib) {
            const char *why = dlerror();
            snprintf(r->error, sizeof(r->error), "dlopen(libSceAudioOut.sprx) failed: %s", why ? why : "unknown");
            return -2;
        }
        audio_out_init_fn init = (audio_out_init_fn)dlsym(lib, "sceAudioOutInit");
        g_open = (audio_out_open_fn)dlsym(lib, "sceAudioOutOpen");
        g_output = (audio_out_output_fn)dlsym(lib, "sceAudioOutOutput");
        g_close = (audio_out_close_fn)dlsym(lib, "sceAudioOutClose");
        if (!init || !g_open || !g_output || !g_close) {
            snprintf(r->error, sizeof(r->error), "libSceAudioOut.sprx does not export the expected functions");
            dlclose(lib);
            return -2;
        }
        g_audio_lib = lib;
        g_get_port_state = (audio_lab_port_state_fn)dlsym(lib, "sceAudioOutGetPortState");
        r->init_called = 1;
        g_init_rc = init();
        g_init_done = g_init_rc == 0;
    }
    r->library_loaded = 1;
    r->init_rc = g_init_rc;
    if (!g_init_done) {
        snprintf(r->error, sizeof(r->error), "sceAudioOutInit returned 0x%08x", (unsigned)g_init_rc);
        return -1;
    }
    return 0;
}

/* g_audio_lock held. One read-only, opt-in measurement of the test's own port (two guarded calls). */
static void measure_port_state(int32_t handle, al_audio_result *r, int point, uint64_t first_start) {
    if (!r->port_state_opt_in || !g_get_port_state || g_port_state_stopped) return;
    audio_lab_state_sample *s = &r->port_state[point];
    if (audio_lab_state_measure(g_get_port_state, handle, g_arena, s) != 0) g_port_state_stopped = 1;
    s->at_us = first_start ? (long long)(p_now_us(NULL) - first_start) : 0;
}

static int p_audio_run(void *u, const al_audio_params *p, al_audio_result *r) {
    (void)u;
    memset(r, 0, sizeof(*r));
    r->call_min_us = -1;
    r->port_state_opt_in = p->measure_port_state;
    unsigned frame_bytes = audio_lab_format_frame_bytes(p->format);
    if (!frame_bytes || !audio_lab_grain_valid(p->grain) || p->rate != AUDIO_LAB_RATE || p->user_id != 0xFF ||
        p->port_type != 0) {
        snprintf(r->error, sizeof(r->error), "Parameters outside the verified SDL driver configuration");
        return -1;
    }
    r->buffer_aligned = ((uintptr_t)g_block & 63u) == 0;
    pthread_mutex_lock(&g_audio_lock); /* one audio run at a time; also protects the static block */
    int loaded = audio_load(r);
    if (loaded != 0) {
        pthread_mutex_unlock(&g_audio_lock);
        return loaded;
    }
    r->open_called = 1;
    int32_t handle = g_open(p->user_id, p->port_type, 0, p->grain, p->rate, (uint32_t)p->format);
    r->open_rc = handle;
    if (handle < 1) {
        snprintf(r->error, sizeof(r->error), "sceAudioOutOpen returned 0x%08x", (unsigned)handle);
        pthread_mutex_unlock(&g_audio_lock);
        return -1;
    }
    r->port_state_exported = g_get_port_state != NULL;
    measure_port_state(handle, r, AUDIO_LAB_STATE_BEFORE_PLAYBACK, 0);
    int result = 0, measured_mid = 0;
    uint64_t first_start = 0, last_end = 0;
    for (unsigned long done = 0; done < p->frames; done += p->grain) {
        if (p->cancel && *p->cancel) {
            r->cancelled = 1;
            break;
        }
        double peak = 0.0;
        unsigned long nonzero = p->fill(p->fill_user, g_block, p->grain, &peak);
        uint64_t t0 = p_now_us(NULL);
        if (!first_start) first_start = t0;
        int32_t rc = g_output(handle, g_block); /* blocks until the system takes the samples */
        uint64_t t1 = p_now_us(NULL);
        last_end = t1;
        r->calls++;
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
            long long us = (long long)(t1 - t0);
            r->call_total_us += us;
            if (r->call_min_us < 0 || us < r->call_min_us) r->call_min_us = us;
            if (us > r->call_max_us) r->call_max_us = us;
        }
        if (!measured_mid && done + p->grain >= p->frames / 2) {
            measure_port_state(handle, r, AUDIO_LAB_STATE_DURING_PLAYBACK, first_start);
            measured_mid = 1;
        }
    }
    r->wall_us = first_start ? (long long)(last_end - first_start) : 0;
    measure_port_state(handle, r, AUDIO_LAB_STATE_BEFORE_CLOSE, first_start);
    r->port_state_stopped = g_port_state_stopped;
    r->close_called = 1;
    r->close_rc = g_close(handle);
    if (r->close_rc != 0 && result == 0) {
        snprintf(r->error, sizeof(r->error), "sceAudioOutClose returned 0x%08x", (unsigned)r->close_rc);
        result = -1;
    }
    if (r->call_min_us < 0) r->call_min_us = 0;
    pthread_mutex_unlock(&g_audio_lock);
    return result;
}

static int p_read_control(void *u, char *buf, size_t len, int *reset) {
    (void)u;
    static off_t offset = 0;
    *reset = 0;
    int fd = open(AL_OUT_DIR "/control", O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size < offset) {
        offset = 0;
        *reset = 1;
    }
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        close(fd);
        return 0;
    }
    ssize_t n = read(fd, buf, len);
    if (n > 0) offset += n;
    close(fd);
    return n > 0 ? (int)n : 0;
}

static int p_port_state_opt_in(void *u) {
    (void)u;
    struct stat st;
    return stat(AL_OUT_DIR "/enable-portstate", &st) == 0;
}

void al_backend_ps5(al_backend *be) {
    al_backend b = { "PlayStation 5", NULL, p_now_unix, p_now_us, p_sleep_ms, p_notify, p_fw, p_pid,
                     p_audio_run, p_read_control, p_port_state_opt_in };
    *be = b;
}
