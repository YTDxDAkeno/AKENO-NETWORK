/*
 * AKENO Party Voice's console functions. Every one has run on firmware 12.20 in an AKENO build:
 *
 *   sceKernelSendNotificationRequest  the daemon, AKENO Native, the Voice Lab (SDK samples/hello_world layout)
 *   kernel_get_fw_version             SDK runtime, read only (daemon, AKENO Native)
 *   sceSystemServiceGetAppIdOfRunningBigApp   weak, read only (AKENO Native; websrv hbldr.c)
 *   sceUserServiceInitialize          the daemon (samples/browser), the Voice Lab
 *   sceUserServiceGetForegroundUser   weak, read only (AKENO Native; websrv hbldr.c)
 *   sceAudioOutInit/Open/Output/Close the audio labs (background payload, 12.20: accepted, paced,
 *                                     not heard) and AKENO Native (foreground, heard). Routes 1-4 are
 *                                     the Voice Lab's four variants: the system or foreground user,
 *                                     port type 0, 2 or 3 (a refused port is reported, nothing else
 *                                     is tried)
 *   sceAudioInOpen/Input/Close        dlopen(libSceAudioIn.sprx) like AKENO Native 1.1 (foreground,
 *                                     12.20: opened at 16 kHz, level followed speech) and the Voice Lab;
 *                                     the PS5-Audio-Passthrough-Research declarations
 *
 * New here: these calls from a background payload while a game runs, for as long as the user stays
 * in voice. Whether the microphone delivers sound and whether the output is audible in that context
 * is exactly what the owner's hardware test decides; nothing here assumes it. No routing, volume,
 * focus, attach/detach, HDMI or system setting is touched (CI: console/check-partyvoice.sh).
 */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "pv_backend.h"

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

#define USER_SYSTEM 0xFF
#define AUDIO_OUT_S16_STEREO 1
#define AUDIO_OUT_ALREADY_INITIALIZED ((int32_t)0x8026000E)
#define AUDIO_IN_TYPE_GENERAL 1
#define AUDIO_IN_FORMAT_S16_MONO 0
#define NO_USER_SERVICE ((int32_t)0x80000001)
#define NO_LIBRARY ((int32_t)0x80000002)

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

static int32_t foreground_user(int32_t *user) {
    if (!g.user_service || !sceUserServiceGetForegroundUser) return NO_USER_SERVICE;
    uint32_t id = 0;
    int rc = sceUserServiceGetForegroundUser(&id);
    if (rc == 0) *user = (int32_t)id;
    return rc;
}

static void p_describe(void *u, char *out, size_t len) {
    (void)u;
    uint32_t fw = kernel_get_fw_version();
    snprintf(out, len, "PS5, firmware %x.%02x (0x%08x), background payload pid %d, user service %s (0x%08X)", (fw >> 24) & 0xFF,
             (fw >> 16) & 0xFF, fw, (int)getpid(), g.user_service ? "ready" : "unavailable", (unsigned)g.user_service_rc);
}

static int32_t p_out_init(void *u) {
    (void)u;
    int32_t rc = sceAudioOutInit();
    return rc == AUDIO_OUT_ALREADY_INITIALIZED ? 0 : rc;
}

static int32_t p_out_open(void *u, int route, char *why, size_t why_len) {
    (void)u;
    const pv_route *r = pv_route_get(route);
    if (!r) return NO_LIBRARY;
    int32_t user = USER_SYSTEM;
    if (r->foreground_user) {
        int32_t rc = foreground_user(&user);
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

/* The foreground user's microphone (DualSense or a headset the system routes to that user):
 * 16 kHz first, 48 kHz as the fallback, exactly as AKENO Native 1.1. */
static int32_t p_mic_open(void *u, unsigned *rate, char *why, size_t why_len) {
    (void)u;
    mic_load();
    if (!g.open) {
        snprintf(why, why_len, "%s", g.why);
        return NO_LIBRARY;
    }
    int32_t user = 0;
    int32_t rc = foreground_user(&user);
    if (rc != 0) {
        snprintf(why, why_len, "the foreground user is unknown (0x%08X)", (unsigned)rc);
        return rc;
    }
    static const unsigned rates[] = { 16000, 48000 };
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        rc = g.open(user, AUDIO_IN_TYPE_GENERAL, 0, PV_MIC_GRAIN, rates[i], AUDIO_IN_FORMAT_S16_MONO);
        if (rc >= 0) {
            *rate = rates[i];
            snprintf(why, why_len, "sceAudioIn via dlopen, user 0x%08X, type 1, %d frames, %u Hz, S16 mono", (unsigned)user, PV_MIC_GRAIN, rates[i]);
            return rc;
        }
    }
    snprintf(why, why_len, "sceAudioInOpen refused 16000 and 48000 Hz for user 0x%08X", (unsigned)user);
    return rc;
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
    pv_backend b = { "PS5", NULL, p_now_us, p_notify, p_bigapp, p_describe, p_out_init, p_out_open, p_out_output, p_out_close,
                     p_mic_open, p_mic_read, p_mic_close };
    *be = b;
}
