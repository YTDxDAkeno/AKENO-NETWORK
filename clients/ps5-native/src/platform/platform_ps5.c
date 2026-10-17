/*
 * PS5 implementation of the platform layer. The only AKENO Native code that declares or calls
 * console system functions. Declarations and where they come from:
 *
 *   sceKernelSendNotificationRequest   SDK samples/hello_world; the daemon uses it (works on 12.20)
 *   kernel_get_fw_version              SDK runtime, include/ps5/kernel.h, read-only (daemon, 12.20)
 *   sceKernelGetAppInfo                weak; the daemon's game detection calls it on 12.20 (other PIDs)
 *   sceSystemServiceGetAppIdOfRunningBigApp   weak; ps5-payload-dev/websrv src/ps5/hbldr.c (read-only query)
 *   sceSystemServiceHideSplashScreen, sceSystemServiceLoadExec   ps5-payload-dev/SDL src/main/ps5 (what
 *                                      every SDL homebrew calls at start and exit)
 *   sceAudioOutInit/Open/Output/Close  ps5-payload-dev/SDL src/audio/ps5 (same arguments as SDL and as
 *                                      audio labs v1/v2, which called them on 12.20 from the background)
 *   sceUserServiceGetForegroundUser    weak; websrv hbldr.c (read-only)
 *   scePadGetHandle, scePadReadState   GetHandle weak; ps5-payload-dev/SDL src/joystick/ps5 and the
 *                                      daemon's shortcut: reads the state of the pad SDL opened
 *
 * Not used, by rule (docs/PS5_NATIVE_ADR.md, CI-enforced): microphone (sceAudioIn*), pad focus or
 * privilege functions, app launch/kill, title registration, mounting, ptrace, the SDK's kernel
 * memory helpers. Every audio call here is AKENO's own port; nothing touches routing or volume.
 */
#include "akn_platform.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

/* Layout from samples/hello_world/main.c of the pinned SDK (as the daemon). */
typedef struct {
    char reserved[45];
    char message[3075];
} notify_request_t;

/* PS5_PadData of ps5-payload-dev/SDL; read into a larger zeroed buffer (as the daemon). */
typedef struct {
    uint32_t buttons;
    uint8_t left_x, left_y, right_x, right_y, l2, r2;
    uint16_t padding;
    float quat[4];
    float vel[3];
    float accel[3];
    struct {
        uint8_t fingers;
        uint8_t pad1[3];
        uint32_t pad2;
        struct {
            uint16_t x, y;
            uint8_t finger;
            uint8_t pad[3];
        } touch[2];
    } touch;
    uint8_t connected;
    uint64_t timestamp;
    uint8_t ext[16];
    uint8_t count;
    uint8_t unknown[15];
} pad_data_t;

#define USER_ID_SYSTEM 0xFF   /* SDL: PROSPERO_USER_SERVICE_USER_ID_SYSTEM */
#define AUDIO_PORT_MAIN 0     /* SDL: PROSPERO_AUDIO_OUT_PORT_TYPE_MAIN */
#define PAD_PORT_STANDARD 0

int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int blocking);
int sceKernelGetAppInfo(pid_t pid, void *info) __attribute__((weak));
int sceSystemServiceGetAppIdOfRunningBigApp(void) __attribute__((weak));
int sceSystemServiceHideSplashScreen(void);
int sceSystemServiceLoadExec(const char *path, const char **argv);
int32_t sceAudioOutInit(void);
int32_t sceAudioOutOpen(int32_t user_id, int32_t type, int32_t index, uint32_t len, uint32_t freq, uint32_t param);
int32_t sceAudioOutOutput(int32_t handle, const void *samples);
int32_t sceAudioOutClose(int32_t handle);
int sceUserServiceGetForegroundUser(uint32_t *user_id) __attribute__((weak));
int scePadGetHandle(int32_t user_id, int32_t type, int32_t index) __attribute__((weak));
int scePadReadState(int32_t handle, void *data);

void akn_platform_info_get(akn_platform_info *info) {
    memset(info, 0, sizeof(*info));
    info->name = "PS5";
    uint32_t fw = kernel_get_fw_version();
    info->firmware_raw = fw;
    /* 0xMMmm.... in binary-coded decimal, e.g. 0x12200000 = 12.20. */
    if (fw) snprintf(info->firmware, sizeof(info->firmware), "%x.%02x", (fw >> 24) & 0xFF, (fw >> 16) & 0xFF);
    else snprintf(info->firmware, sizeof(info->firmware), "unknown");
    snprintf(info->akeno_dir, sizeof(info->akeno_dir), "/data/akeno");
    snprintf(info->native_dir, sizeof(info->native_dir), "/data/akeno-native");
    info->daemon_port = 8642;
}

uint64_t akn_platform_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

uint64_t akn_platform_now_ms(void) { return akn_platform_now_us() / 1000u; }

long long akn_platform_unix_time(void) { return (long long)time(NULL); }

akn_context_kind akn_platform_context(akn_context_raw *raw) {
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
    return akn_context_classify(raw);
}

int akn_platform_notify(const char *text) {
    notify_request_t request;
    memset(&request, 0, sizeof(request));
    snprintf(request.message, sizeof(request.message), "%s", text);
    return sceKernelSendNotificationRequest(0, &request, sizeof(request), 0) == 0 ? 0 : -1;
}

void akn_platform_pad_status(akn_pad_status *status) {
    memset(status, 0, sizeof(*status));
    status->available = scePadGetHandle != NULL && sceUserServiceGetForegroundUser != NULL;
    if (!status->available) return;
    uint32_t user = 0;
    status->rc = sceUserServiceGetForegroundUser(&user);
    if (status->rc != 0) return;
    status->user_id = (int32_t)user;
    int handle = scePadGetHandle((int32_t)user, PAD_PORT_STANDARD, 0); /* the handle SDL opened, if any */
    if (handle < 0) {
        status->rc = handle;
        return;
    }
    status->has_handle = 1;
    union {
        pad_data_t data;
        unsigned char raw[512];
    } u;
    memset(&u, 0, sizeof(u));
    status->rc = scePadReadState(handle, &u.data);
    if (status->rc == 0) status->connected = u.data.connected != 0;
}

void akn_platform_before_video(void) { sceSystemServiceHideSplashScreen(); }

void akn_platform_exit_to_system(void) { sceSystemServiceLoadExec("exit", NULL); }

/* ---- Audio: AKENO's own port on the system user's main output -------------------------------- */

static int32_t ps5_init(void *user) {
    (void)user;
    return sceAudioOutInit();
}

static int32_t ps5_open(void *user, unsigned grain, unsigned rate, int format) {
    (void)user;
    return sceAudioOutOpen(USER_ID_SYSTEM, AUDIO_PORT_MAIN, 0, grain, rate, (uint32_t)format);
}

static int32_t ps5_output(void *user, int32_t handle, const void *samples) {
    (void)user;
    return sceAudioOutOutput(handle, samples);
}

static int32_t ps5_close(void *user, int32_t handle) {
    (void)user;
    return sceAudioOutClose(handle);
}

static uint64_t ps5_now_us(void *user) {
    (void)user;
    return akn_platform_now_us();
}

const akn_audio_ops *akn_platform_audio_ops(void) {
    static const akn_audio_ops ops = { ps5_init, ps5_open, ps5_output, ps5_close, ps5_now_us, NULL,
                                       "sceAudioOut, direct (PS5)" };
    return &ops;
}
