/*
 * AKENO Party Voice's console functions. Every one has run on firmware 12.20 in an AKENO build:
 *
 *   sceKernelSendNotificationRequest  the daemon, AKENO Native, the Voice Lab (SDK samples/hello_world layout)
 *   kernel_get_fw_version             SDK runtime, read only (daemon, AKENO Native)
 *   sceKernelGetAppInfo               weak, read only, for this process (AKENO Native's context check, which
 *                                     told the foreground app from an ELF-loader start on 12.20; the daemon
 *                                     calls it for other processes)
 *   sceSystemServiceGetAppIdOfRunningBigApp   weak, read only (AKENO Native; websrv hbldr.c)
 *   sceUserServiceInitialize          the daemon (samples/browser), the Voice Lab, this service 0.1.0
 *   sceUserServiceGetForegroundUser   weak, read only (AKENO Native; websrv hbldr.c)
 *   sceAudioOutInit/Open/Output/Close AKENO Native (foreground, heard on 12.20) and the audio labs and this
 *                                     service 0.1.0 (background payload, 12.20: accepted, not heard). Route 1 is
 *                                     AKENO Native's call exactly; route 2 the foreground user's main port
 *                                     (0x80260011 on 12.20, PS4 name SYSTEM_RESOURCE). Retired routes 3 and 4
 *                                     never reach this file
 *   sceAudioInOpen/Input/Close        dlopen(libSceAudioIn.sprx) like AKENO Native 1.1 (foreground, 12.20: opened
 *                                     at 16 kHz, level followed speech). From this background service 0.1.0 on
 *                                     12.20: 0x809B0081 (the code scePadOpen gives background payloads)
 *
 * No routing, volume, focus, attach/detach, HDMI or system setting is touched, and no other
 * process is acted on (CI: console/check-partyvoice.sh).
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "pv_backend.h"

typedef struct {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int blocking);
int sceKernelGetAppInfo(pid_t pid, void *info) __attribute__((weak));
int sceSystemServiceGetAppIdOfRunningBigApp(void) __attribute__((weak));
int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(uint32_t *user_id) __attribute__((weak));
int32_t sceAudioOutInit(void);
int32_t sceAudioOutOpen(int32_t user_id, int32_t type, int32_t index, uint32_t len, uint32_t freq, uint32_t param);
int32_t sceAudioOutOutput(int32_t handle, const void *samples);
int32_t sceAudioOutClose(int32_t handle);

#define USER_SYSTEM 0xFF
#define AUDIO_OUT_S16_STEREO 1
#define AUDIO_OUT_ALREADY_INITIALIZED ((int32_t)0x8026000E)
#define AUDIO_IN_TYPE_GENERAL 1
#define AUDIO_IN_FORMAT_S16_MONO 0

static struct {
    int user_service;
    int32_t user_service_rc;
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

static int32_t p_foreground_user(void *u, int32_t *user) {
    (void)u;
    if (!g.user_service || !sceUserServiceGetForegroundUser) return PV_NO_USER_SERVICE;
    uint32_t id = 0;
    int rc = sceUserServiceGetForegroundUser(&id);
    if (rc == 0) *user = (int32_t)id;
    return rc;
}

static void p_describe(void *u, char *out, size_t len) {
    (void)u;
    uint32_t fw = kernel_get_fw_version();
    snprintf(out, len, "PS5, firmware %x.%02x (0x%08x), pid %d, user service %s (0x%08X)", (fw >> 24) & 0xFF, (fw >> 16) & 0xFF, fw,
             (int)getpid(), g.user_service ? "ready" : "unavailable", (unsigned)g.user_service_rc);
}

/* AKENO Native's akn_platform_context(), for this process only. */
static void p_process(void *u, akn_context_raw *raw) {
    (void)u;
    memset(raw, 0, sizeof(*raw));
    raw->have_appinfo_fn = sceKernelGetAppInfo != NULL;
    if (raw->have_appinfo_fn) {
        unsigned char info[512];
        memset(info, 0, sizeof(info)); /* oversized and zeroed: the structure is undocumented */
        raw->appinfo_rc = sceKernelGetAppInfo(getpid(), info);
        memcpy(raw->appinfo_raw, info, sizeof(raw->appinfo_raw));
    }
    raw->have_bigapp_fn = sceSystemServiceGetAppIdOfRunningBigApp != NULL;
    if (raw->have_bigapp_fn) raw->bigapp_app_id = sceSystemServiceGetAppIdOfRunningBigApp();
    akn_context_decode(raw);
}

static int32_t p_out_init(void *u) {
    (void)u;
    int32_t rc = sceAudioOutInit();
    return rc == AUDIO_OUT_ALREADY_INITIALIZED ? 0 : rc;
}

static int32_t p_out_open(void *u, int route, char *why, size_t why_len) {
    const pv_route *r = pv_route_get(route);
    if (!r || r->retired) return PV_NO_LIBRARY;
    int32_t user = USER_SYSTEM;
    if (r->foreground_user) {
        int32_t rc = p_foreground_user(u, &user);
        if (rc != 0) {
            snprintf(why, why_len, "the foreground user is unknown (0x%08X)", (unsigned)rc);
            return rc;
        }
    }
    snprintf(why, why_len, "sceAudioOut, user 0x%08X, port type %d, 48000 Hz, S16 stereo, %d frames", (unsigned)user, r->port_type,
             PV_OUT_GRAIN);
    return sceAudioOutOpen(user, r->port_type, 0, PV_OUT_GRAIN, PV_OUT_RATE, AUDIO_OUT_S16_STEREO);
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

/* The user's microphone (DualSense or a headset the system routes to that user), the one
 * configuration asked for: AKENO Native 1.1's call, sceAudioInOpen(user, 1, 0, 256, rate, 0). */
static int32_t p_mic_open(void *u, int32_t user, unsigned rate, char *why, size_t why_len) {
    (void)u;
    mic_load();
    if (!g.open) {
        snprintf(why, why_len, "%s", g.why);
        return PV_NO_LIBRARY;
    }
    snprintf(why, why_len, "sceAudioIn via dlopen, user 0x%08X, type 1, %d frames, %u Hz, S16 mono", (unsigned)user, PV_MIC_GRAIN, rate);
    return g.open(user, AUDIO_IN_TYPE_GENERAL, 0, PV_MIC_GRAIN, rate, AUDIO_IN_FORMAT_S16_MONO);
}

static int32_t p_mic_read(void *u, int32_t handle, int16_t *samples) {
    (void)u;
    return g.input(handle, samples);
}

static int32_t p_mic_close(void *u, int32_t handle) {
    (void)u;
    return g.close(handle);
}

void pv_backend_ps5(pv_backend *be) {
    g.user_service_rc = sceUserServiceInitialize(NULL);
    g.user_service = g.user_service_rc == 0;
    pv_backend b = { "PS5", NULL, p_now_us, p_notify, p_bigapp, p_describe, p_process, p_foreground_user, p_out_init, p_out_open,
                     p_out_output, p_out_close, p_mic_open, p_mic_read, p_mic_close };
    *be = b;
}
