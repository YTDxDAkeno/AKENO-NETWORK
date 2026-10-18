/*
 * The Voice Lab's console functions. Every one has run on firmware 12.20 in an AKENO build before:
 *
 *   sceKernelSendNotificationRequest  the daemon and AKENO Native (SDK samples/hello_world layout)
 *   kernel_get_fw_version             SDK runtime, read only (daemon, AKENO Native)
 *   sceSystemServiceGetAppIdOfRunningBigApp   weak, read only (AKENO Native; websrv hbldr.c)
 *   sceUserServiceInitialize          the daemon (samples/browser)
 *   sceUserServiceGetForegroundUser   weak, read only (AKENO Native; websrv hbldr.c)
 *   sceAudioOutInit/Open/Output/Close the audio labs (background payload, 12.20: accepted, paced,
 *                                     not heard) and AKENO Native (foreground, heard)
 *   sceAudioInOpen/Input/Close        dlopen(libSceAudioIn.sprx) like AKENO Native 1.1 (foreground,
 *                                     12.20: opened at 16 kHz, level followed speech); the same
 *                                     declarations (PS5-Audio-Passthrough-Research)
 *
 * New here, and the point of the lab: these calls from a background payload while a game runs,
 * the foreground user's ID for audio output, and the port types 2 and 3 (PS4 numbering; a refusal
 * is reported, nothing else is attempted). No routing, volume, focus or system setting is touched.
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "backend.h"
#include "lab_net.h"

typedef struct {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int blocking);
int sceSystemServiceGetAppIdOfRunningBigApp(void) __attribute__((weak));
int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(uint32_t *user_id) __attribute__((weak));
int32_t sceAudioOutInit(void);
int32_t sceAudioOutOpen(int32_t user_id, int32_t type, int32_t index, uint32_t len, uint32_t freq, uint32_t param);
int32_t sceAudioOutOutput(int32_t handle, const void *samples);
int32_t sceAudioOutClose(int32_t handle);

#define AUDIO_OUT_S16_STEREO 1
#define AUDIO_IN_TYPE_GENERAL 1
#define AUDIO_IN_FORMAT_S16_MONO 0

static struct {
    int user_service;
    int mic_loaded;
    void *lib;
    int32_t (*open)(int32_t user, uint32_t type, uint32_t index, uint32_t grain, uint32_t freq, uint32_t format);
    int32_t (*input)(int32_t handle, void *dest);
    int32_t (*close)(int32_t handle);
    char why[160];
} g;

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

static int32_t p_bigapp(void *u) {
    (void)u;
    return sceSystemServiceGetAppIdOfRunningBigApp ? sceSystemServiceGetAppIdOfRunningBigApp() : -1;
}

static int32_t p_user(void *u, int32_t *user) {
    (void)u;
    if (!g.user_service || !sceUserServiceGetForegroundUser) return (int32_t)0x80000001;
    uint32_t id = 0;
    int rc = sceUserServiceGetForegroundUser(&id);
    if (rc == 0) *user = (int32_t)id;
    return rc;
}

static void p_describe(void *u, char *out, size_t len) {
    (void)u;
    uint32_t fw = kernel_get_fw_version();
    snprintf(out, len, "PS5, firmware %x.%02x (0x%08x), background payload pid %d, user service %s, AudioIn %s",
             (fw >> 24) & 0xFF, (fw >> 16) & 0xFF, fw, (int)getpid(), g.user_service ? "ready" : "unavailable",
             g.open ? "resolved" : (g.why[0] ? g.why : "not loaded"));
}

static int32_t p_out_init(void *u) {
    (void)u;
    int32_t rc = sceAudioOutInit();
    return rc == (int32_t)0x8026000E ? 0 : rc; /* already initialised */
}

static int32_t p_out_open(void *u, int32_t user, int32_t type, unsigned grain) {
    (void)u;
    return sceAudioOutOpen(user, type, 0, grain, 48000, AUDIO_OUT_S16_STEREO);
}

static int32_t p_out_output(void *u, int32_t handle, const int16_t *stereo) {
    (void)u;
    return sceAudioOutOutput(handle, stereo);
}

static int32_t p_out_close(void *u, int32_t handle) {
    (void)u;
    return sceAudioOutClose(handle);
}

static void mic_load(void) {
    if (g.mic_loaded) return;
    g.mic_loaded = 1;
    g.lib = dlopen("libSceAudioIn.sprx", RTLD_LAZY);
    if (!g.lib) {
        const char *e = dlerror();
        snprintf(g.why, sizeof(g.why), "dlopen(libSceAudioIn.sprx) failed: %s", e ? e : "unknown");
        return;
    }
    *(void **)&g.open = dlsym(g.lib, "sceAudioInOpen");
    *(void **)&g.input = dlsym(g.lib, "sceAudioInInput");
    *(void **)&g.close = dlsym(g.lib, "sceAudioInClose");
    if (!g.open || !g.input || !g.close) {
        g.open = NULL;
        snprintf(g.why, sizeof(g.why), "libSceAudioIn.sprx lacks sceAudioInOpen/Input/Close");
    }
}

static int32_t p_mic_open(void *u, int32_t user, char *why, size_t why_len) {
    (void)u;
    mic_load();
    if (!g.open) {
        snprintf(why, why_len, "%s", g.why);
        return (int32_t)0x80000002;
    }
    snprintf(why, why_len, "sceAudioIn via dlopen, user 0x%08x, type 1, 256 frames, 16000 Hz, S16 mono", (unsigned)user);
    return g.open(user, AUDIO_IN_TYPE_GENERAL, 0, 256, 16000, AUDIO_IN_FORMAT_S16_MONO);
}

static int32_t p_mic_read(void *u, int32_t handle, int16_t *samples) {
    (void)u;
    return g.input(handle, samples);
}

static int32_t p_mic_close(void *u, int32_t handle) {
    (void)u;
    return g.close(handle);
}

static int p_probe_daemon(void *u, int *ms) {
    (void)u;
    return akn_lab_probe_http("127.0.0.1", 8642, "/api/instance", 2000, ms);
}

static int p_probe_server(void *u, int *ms, char *host, size_t host_len) {
    (void)u;
    int port = 0;
    if (akn_lab_server_from_settings("/data/akeno/settings.json", host, host_len, &port) != 0) {
        *ms = 0;
        return -1;
    }
    return akn_lab_probe_tcp(host, port, 5000, ms);
}

void akn_lab_backend_ps5(akn_lab_backend *be) {
    g.user_service = sceUserServiceInitialize(NULL) == 0;
    akn_lab_backend b = { "PS5", p_now_us, p_sleep_ms, p_notify, p_bigapp, p_user, p_describe,
                          p_out_init, p_out_open, p_out_output, p_out_close, p_mic_open, p_mic_read, p_mic_close,
                          p_probe_daemon, p_probe_server, NULL };
    *be = b;
}
