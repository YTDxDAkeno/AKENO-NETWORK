/*
 * PS5 platform layer. Each system call below is used exactly as in a sample of the pinned
 * ps5-payload-sdk (commit f5d40f02770e9b0001dd9e18056eca70b3da8b40); no other system
 * interface, offset or patch is used by AKENO code:
 *
 *   sceKernelSendNotificationRequest  samples/hello_world   (verified on a PS5 with 12.20)
 *   sceUserServiceInitialize          samples/browser       (verified on a PS5 with 12.20)
 *   sceSystemServiceLaunchWebBrowser  samples/browser       (verified on a PS5 with 12.20)
 *   sysctl(KERN_PROC_PROC)            samples/ps            standard FreeBSD process list, read-only
 *   sceKernelGetAppInfo               samples/ps            read-only application info per process
 *   sceNotificationSend               samples/notify        experimental, opt-in (button notification),
 *                                                           resolved at run time, see below
 *   dlopen / dlsym                    samples/hello_dlfcn   only for libSceNotification.sprx
 *   kernel_get_fw_version             include/ps5/kernel.h  read-only query in the SDK runtime
 *   klog_puts                         include/ps5/klog.h    debug log channel of the SDK runtime
 *
 * Experimental controller shortcut (off by default, Settings -> Quick Access), resolved at run
 * time with dlopen/dlsym only after the user turns it on:
 *   scePadInit/Open/GetHandle/ReadState/Close  ps5-payload-dev/SDL  read-only controller state
 *                                              (src/joystick/ps5, commit ee4c47d)
 *   sceUserServiceGetLoginUserIdList           ps5-payload-dev/SDL  which user's controller to read
 *   sceUserServiceGetForegroundUser            ps5-payload-dev/websrv (src/ps5/hbldr.c)
 *
 * Audio lab builds only (AKENO_EXPERIMENTAL_AUDIO, never the release ELF), user-started:
 *   dlopen / dlsym                    samples/hello_dlfcn   libSceAudioOut.sprx, libSceAudioIn.sprx:
 *                                                           symbol lookups (prototype A)
 *   sceAudioOutInit/Open/Output/Close ps5-payload-dev/SDL   prototypes and constants from its PS5 audio
 *                                                           driver (src/audio/ps5, commit ee4c47d);
 *                                                           resolved at run time (prototype C)
 *
 * Game detection never attaches to, injects into or modifies another process.
 *
 * Why sceNotificationSend is not linked: the SDK runtime (crt/rtld_payload.c) refuses to start a
 * payload when a DT_NEEDED library cannot be loaded or a symbol cannot be resolved. Linking
 * libSceNotification would therefore put the proven start-up of Alpha 0.1 at risk on a firmware
 * where the library or symbol is unavailable. It is loaded with dlopen() only when the user has
 * turned on the experimental button notifications; any failure just disables them, and every
 * notification keeps using sceKernelSendNotificationRequest.
 */
#include "platform.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>

/* Layout from samples/hello_world/main.c of the pinned SDK. */
typedef struct {
    char reserved[45];
    char message[3075];
} notify_request_t;

/*
 * sceKernelGetAppInfo writes an undocumented structure; samples/ps and samples/test_privileges
 * disagree on where its title ID is (offset 20 or 16), and both declare at most 0x5a bytes. It is
 * given a zeroed buffer of APP_INFO_BYTES and decoded by akeno_appinfo_parse(), which records
 * both candidates; game_detect.c decides from the console's own answers (see game_detect.h).
 */
#define APP_INFO_BYTES 512

#define SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM 0xFE /* samples/notify */

int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int blocking);
int sceUserServiceInitialize(void *params);
int sceUserServiceTerminate(void);
int sceSystemServiceLaunchWebBrowser(const char *uri, void *params);
/*
 * Weak import: the SDK runtime skips unresolved weak symbols instead of refusing to start the
 * payload (crt/rtld_payload.c), the same pattern the SDK's libc uses for dlopen. If a firmware
 * does not export it, game detection reports "unsupported" and everything else runs as before.
 * console/check-imports.sh requires this import to stay weak.
 */
int sceKernelGetAppInfo(pid_t pid, void *info) __attribute__((weak));

/* samples/notify: int sceNotificationSend(int userId, bool isLogged, const char *payload). */
typedef int (*notification_send_fn)(int user_id, bool is_logged, const char *payload);

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_user_service = 0;
static void *g_notification_lib = NULL;           /* g_lock */
static notification_send_fn g_notification_send;  /* g_lock */
static int g_notification_tried = 0;              /* g_lock */
static char g_notification_error[96] = "not loaded (button notifications are off)";

void akeno_platform_init(akeno_platform_info *info) {
    memset(info, 0, sizeof(*info));
    info->name = "PlayStation 5";
    unsigned int fw = kernel_get_fw_version();
    info->firmware_raw = fw;
    /* 0xMMmm.... in binary-coded decimal, e.g. 0x12200000 = 12.20. */
    if (fw) snprintf(info->firmware, sizeof(info->firmware), "%x.%02x", (fw >> 24) & 0xFF, (fw >> 16) & 0xFF);
    info->notifications = "sceKernelSendNotificationRequest";
    info->browser = "sceSystemServiceLaunchWebBrowser";
    info->game_detection = sceKernelGetAppInfo ? "sysctl(KERN_PROC_PROC) + sceKernelGetAppInfo (read-only)"
                                               : "unavailable: sceKernelGetAppInfo is not exported by this firmware";
    info->actionable = "sceNotificationSend via dlopen(libSceNotification.sprx) (InteractiveToastTemplateB, experimental, opt-in)";
    /* Required before the browser can be launched (as in samples/browser). */
    g_user_service = sceUserServiceInitialize(NULL) == 0;
}

void akeno_platform_cleanup(void) {
    pthread_mutex_lock(&g_lock);
    if (g_notification_lib) dlclose(g_notification_lib);
    g_notification_lib = NULL;
    g_notification_send = NULL;
    pthread_mutex_unlock(&g_lock);
    if (g_user_service) sceUserServiceTerminate();
    g_user_service = 0;
}

int akeno_platform_notify(const char *text) {
    notify_request_t request;
    memset(&request, 0, sizeof(request));
    snprintf(request.message, sizeof(request.message), "%s", text);
    pthread_mutex_lock(&g_lock);
    int rc = sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    pthread_mutex_unlock(&g_lock);
    return rc == 0 ? 0 : -1;
}

int akeno_platform_open_browser(const char *url) {
    pthread_mutex_lock(&g_lock);
    int rc = sceSystemServiceLaunchWebBrowser(url, NULL);
    pthread_mutex_unlock(&g_lock);
    return rc == 0 ? 0 : -1;
}

/* ---- Game detection ----------------------------------------------------------------------- */

int akeno_platform_list_apps(akeno_proc_info *out, size_t max, size_t *total, char *err, size_t err_len) {
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t size = 0;
    *total = 0;
    if (!sceKernelGetAppInfo) {
        snprintf(err, err_len, "sceKernelGetAppInfo is not available on this firmware.");
        return -1;
    }
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0 || size == 0) {
        snprintf(err, err_len, "The process list is not available (errno %d).", errno);
        return -1;
    }
    size += size / 4 + 16 * sizeof(struct kinfo_proc); /* Room for processes started in between. */
    char *buf = malloc(size);
    if (!buf) {
        snprintf(err, err_len, "Out of memory.");
        return -1;
    }
    if (sysctl(mib, 4, buf, &size, NULL, 0) != 0) {
        snprintf(err, err_len, "The process list could not be read (errno %d).", errno);
        free(buf);
        return -1;
    }
    unsigned char *raw = calloc(1, APP_INFO_BYTES);
    if (!raw) {
        free(buf);
        snprintf(err, err_len, "Out of memory.");
        return -1;
    }
    size_t n = 0;
    for (char *p = buf; p + sizeof(int) <= buf + size;) {
        const struct kinfo_proc *ki = (const struct kinfo_proc *)p;
        if (ki->ki_structsize < (int)sizeof(struct kinfo_proc) || p + ki->ki_structsize > buf + size) {
            if (ki->ki_structsize != 0) snprintf(err, err_len, "Unexpected process record size %d.", ki->ki_structsize);
            break;
        }
        p += ki->ki_structsize;
        (*total)++;
        if (n >= max) continue;
        akeno_proc_info *o = &out[n++];
        memset(o, 0, sizeof(*o));
        o->pid = ki->ki_pid;
        o->state = ki->ki_stat == SSTOP || ki->ki_stat == SZOMB ? AKENO_PROC_STOPPED : AKENO_PROC_ACTIVE;
        snprintf(o->comm, sizeof(o->comm), "%.*s", (int)sizeof(ki->ki_comm), ki->ki_comm);
        /* Zeroed and much larger than either declared structure, in case the system writes more. */
        memset(raw, 0, APP_INFO_BYTES);
        if (sceKernelGetAppInfo(ki->ki_pid, raw) != 0) continue;
        o->has_app_info = 1;
        akeno_appinfo_parse(raw, APP_INFO_BYTES, o);
    }
    free(raw);
    free(buf);
    return (int)n;
}

const char *akeno_platform_metadata_root(void) { return ""; }

/* ---- Experimental button notification ----------------------------------------------------- */

/* Appends s to out as JSON string content (without quotes). */
static void json_escape(char *out, size_t out_len, const char *s) {
    size_t o = strlen(out);
    for (const unsigned char *p = (const unsigned char *)s; *p && o + 7 < out_len; p++) {
        if (*p == '"' || *p == '\\') {
            out[o++] = '\\';
            out[o++] = (char)*p;
        } else if (*p < 0x20) {
            o += (size_t)snprintf(out + o, out_len - o, "\\u%04x", *p);
        } else {
            out[o++] = (char)*p;
        }
    }
    out[o] = '\0';
}

/* Loads libSceNotification.sprx once (g_lock held). Returns the function or NULL. */
static notification_send_fn notification_send_locked(void) {
    if (g_notification_tried) return g_notification_send;
    g_notification_tried = 1;
    g_notification_lib = dlopen("libSceNotification.sprx", RTLD_LAZY);
    if (!g_notification_lib) {
        const char *why = dlerror();
        snprintf(g_notification_error, sizeof(g_notification_error), "dlopen failed: %s", why ? why : "unknown error");
        return NULL;
    }
    g_notification_send = (notification_send_fn)dlsym(g_notification_lib, "sceNotificationSend");
    if (!g_notification_send) {
        const char *why = dlerror();
        snprintf(g_notification_error, sizeof(g_notification_error), "dlsym failed: %s", why ? why : "unknown error");
        dlclose(g_notification_lib);
        g_notification_lib = NULL;
        return NULL;
    }
    snprintf(g_notification_error, sizeof(g_notification_error), "loaded");
    return g_notification_send;
}

const char *akeno_platform_actionable_status(void) {
    /* Read without the lock: a short, always NUL-terminated diagnostic string. */
    return g_notification_error;
}

/* Same template as samples/notify, with AKENO's text and one DeepLink action. */
int akeno_platform_notify_action(const char *title, const char *text, const char *button, const char *url) {
    static unsigned int counter = 0;
    char t[400] = "", m[600] = "", b[120] = "", u[400] = "";
    json_escape(t, sizeof(t), title);
    json_escape(m, sizeof(m), text);
    json_escape(b, sizeof(b), button);
    json_escape(u, sizeof(u), url);
    char stamp[40] = "";
    time_t now = time(NULL);
    struct tm tm;
    if (gmtime_r(&now, &tm)) strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S.000Z", &tm);
    char *payload = malloc(4096);
    if (!payload) return -1;
    pthread_mutex_lock(&g_lock);
    notification_send_fn send = notification_send_locked();
    if (!send) {
        pthread_mutex_unlock(&g_lock);
        free(payload);
        return -2;
    }
    unsigned int id = 0x41000000u + ((unsigned int)now & 0xFFFFF) * 16 + (counter++ & 15);
    snprintf(payload, 4096,
             "{\"rawData\":{\"viewTemplateType\":\"InteractiveToastTemplateB\",\"channelType\":\"Downloads\",\"useCaseId\":\"IDC\","
             "\"toastOverwriteType\":\"No\",\"isImmediate\":true,\"priority\":100,\"viewData\":{"
             "\"icon\":{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"download\"}},"
             "\"message\":{\"body\":\"%s\"},\"subMessage\":{\"body\":\"%s\"},"
             "\"actions\":[{\"actionName\":\"%s\",\"actionType\":\"DeepLink\",\"defaultFocus\":true,\"parameters\":{\"actionUrl\":\"%s\"}}]},"
             "\"platformViews\":{\"previewDisabled\":{\"viewData\":{\"icon\":{\"type\":\"Predefined\",\"parameters\":{\"icon\":\"download\"}},"
             "\"message\":{\"body\":\"%s\"}}}}},"
             "\"createdDateTime\":\"%s\",\"localNotificationId\":\"%u\"}",
             t, m, b, u, t, stamp, id);
    int rc = send(SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM, true, payload);
    pthread_mutex_unlock(&g_lock);
    free(payload);
    return rc == 0 ? 0 : -1;
}

void akeno_platform_debug_line(const char *line) {
    klog_puts(line);
}

/* ---- Experimental controller shortcut (opt-in) -------------------------------------------- */

/*
 * Declarations and constants of the PS5 joystick driver of ps5-payload-dev/SDL
 * (src/joystick/ps5/SDL_ps5joystick.h, commit ee4c47d), which reads the DualSense from homebrew;
 * sceUserServiceGetForegroundUser as declared in ps5-payload-dev/websrv (src/ps5/hbldr.c). Only
 * the read-only subset is used: no vibration, light bar, focus, privilege or remapping call.
 * Resolved with dlopen()/dlsym() when the user turns the shortcut on, never linked.
 */
typedef int (*pad_init_fn)(void);
typedef int (*pad_open_fn)(int user_id, int type, int index, void *param);
typedef int (*pad_get_handle_fn)(int user_id, int type, int index);
typedef int (*pad_read_state_fn)(int handle, void *data);
typedef int (*pad_close_fn)(int handle);
typedef int (*user_foreground_fn)(uint32_t *user_id);
typedef int (*user_login_list_fn)(int user_ids[4]);
#define PAD_PORT_TYPE_STANDARD 0
#define PAD_ERROR_ALREADY_OPENED ((int)0x80920004u)
#define PAD_MAX_USERS 4

/* PS5_PadData of the SDL driver; read into a larger zeroed buffer in case the system writes more. */
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

static pthread_mutex_t g_pad_lock = PTHREAD_MUTEX_INITIALIZER;
static void *g_pad_lib = NULL;          /* g_pad_lock; kept loaded once loaded */
static pad_init_fn g_pad_init;
static pad_open_fn g_pad_open;
static pad_get_handle_fn g_pad_get_handle;
static pad_read_state_fn g_pad_read_state;
static pad_close_fn g_pad_close;
static int g_pad_init_done = 0;         /* scePadInit succeeded (called once per process, as SDL does) */
static int g_pad_init_rc = 0;
static int g_pad_handle = -1;
static int g_pad_handle_owned = 0;      /* Opened by scePadOpen here (closed again); 0 for scePadGetHandle. */

static int pad_load_locked(char *err, size_t err_len) {
    if (g_pad_lib) return 0;
    void *lib = dlopen("libScePad.sprx", RTLD_LAZY);
    if (!lib) {
        const char *why = dlerror();
        snprintf(err, err_len, "dlopen(libScePad.sprx) failed: %s", why ? why : "unknown error");
        return -2;
    }
    g_pad_init = (pad_init_fn)dlsym(lib, "scePadInit");
    g_pad_open = (pad_open_fn)dlsym(lib, "scePadOpen");
    g_pad_get_handle = (pad_get_handle_fn)dlsym(lib, "scePadGetHandle");
    g_pad_read_state = (pad_read_state_fn)dlsym(lib, "scePadReadState");
    g_pad_close = (pad_close_fn)dlsym(lib, "scePadClose");
    if (!g_pad_init || !g_pad_open || !g_pad_get_handle || !g_pad_read_state || !g_pad_close) {
        snprintf(err, err_len, "libScePad.sprx does not export the expected functions");
        dlclose(lib);
        return -2;
    }
    g_pad_lib = lib;
    return 0;
}

/* The user whose controller to read: the foreground user, else the first logged-in user. */
static int pad_find_user(akeno_pad_info *info) {
    void *lib = dlopen("libSceUserService.sprx", RTLD_LAZY); /* Already loaded by the app (linked). */
    if (!lib) {
        snprintf(info->user_source, sizeof(info->user_source), "user service not loadable");
        return -1;
    }
    user_foreground_fn foreground = (user_foreground_fn)dlsym(lib, "sceUserServiceGetForegroundUser");
    user_login_list_fn login_list = (user_login_list_fn)dlsym(lib, "sceUserServiceGetLoginUserIdList");
    int user = -1;
    uint32_t id = 0;
    if (foreground && foreground(&id) == 0 && id != 0 && id != 0xFFFFFFFFu) {
        user = (int)id;
        snprintf(info->user_source, sizeof(info->user_source), "foreground user");
    } else if (login_list) {
        int ids[PAD_MAX_USERS] = { -1, -1, -1, -1 };
        if (login_list(ids) == 0) {
            for (int i = 0; i < PAD_MAX_USERS && user < 0; i++)
                if (ids[i] != -1) user = ids[i];
        }
        snprintf(info->user_source, sizeof(info->user_source), user >= 0 ? "first logged-in user" : "no logged-in user");
    } else {
        snprintf(info->user_source, sizeof(info->user_source), "user functions not found");
    }
    dlclose(lib); /* Drops only this reference; the linked library stays loaded. */
    return user;
}

int akeno_platform_pad_open(akeno_pad_info *info, char *err, size_t err_len) {
    memset(info, 0, sizeof(*info));
    info->user_id = -1;
    info->handle = -1;
    snprintf(info->method, sizeof(info->method),
             "scePadReadState via dlopen(libScePad.sprx), read-only (declarations of ps5-payload-dev/SDL)");
    pthread_mutex_lock(&g_pad_lock);
    int rc = pad_load_locked(err, err_len);
    if (rc != 0) {
        pthread_mutex_unlock(&g_pad_lock);
        return rc;
    }
    info->library_loaded = 1;
    if (!g_pad_init_done) {
        g_pad_init_rc = g_pad_init();
        g_pad_init_done = g_pad_init_rc == 0;
        info->init_called = 1;
    }
    info->init_rc = g_pad_init_rc;
    if (!g_pad_init_done) {
        snprintf(err, err_len, "scePadInit returned 0x%08x", (unsigned)g_pad_init_rc);
        pthread_mutex_unlock(&g_pad_lock);
        return -1;
    }
    if (g_pad_handle >= 0 && g_pad_handle_owned) g_pad_close(g_pad_handle);
    g_pad_handle = -1;
    g_pad_handle_owned = 0;
    info->user_id = pad_find_user(info);
    if (info->user_id < 0) {
        snprintf(err, err_len, "No signed-in user was found (%s)", info->user_source);
        pthread_mutex_unlock(&g_pad_lock);
        return -1;
    }
    int handle = g_pad_open(info->user_id, PAD_PORT_TYPE_STANDARD, 0, NULL);
    info->open_rc = handle;
    if (handle == PAD_ERROR_ALREADY_OPENED) {
        handle = g_pad_get_handle(info->user_id, PAD_PORT_TYPE_STANDARD, 0);
        info->shared_handle = 1;
    }
    if (handle < 0) {
        snprintf(err, err_len, "%s returned 0x%08x", info->shared_handle ? "scePadGetHandle" : "scePadOpen", (unsigned)handle);
        pthread_mutex_unlock(&g_pad_lock);
        return -1;
    }
    g_pad_handle = handle;
    g_pad_handle_owned = !info->shared_handle;
    info->handle = handle;
    pthread_mutex_unlock(&g_pad_lock);
    return 0;
}

int akeno_platform_pad_read(unsigned *buttons, int *connected, int *rc) {
    union {
        pad_data_t data;
        unsigned char raw[512];
    } u;
    memset(&u, 0, sizeof(u));
    *buttons = 0;
    *connected = 0;
    pthread_mutex_lock(&g_pad_lock);
    if (g_pad_handle < 0 || !g_pad_read_state) {
        pthread_mutex_unlock(&g_pad_lock);
        *rc = -1;
        return -1;
    }
    *rc = g_pad_read_state(g_pad_handle, &u.data);
    pthread_mutex_unlock(&g_pad_lock);
    if (*rc != 0) return -1;
    *buttons = u.data.buttons;
    *connected = u.data.connected != 0;
    return 0;
}

void akeno_platform_pad_close(void) {
    pthread_mutex_lock(&g_pad_lock);
    /* Only a handle opened here with scePadOpen is closed; one from scePadGetHandle is left alone. */
    if (g_pad_handle >= 0 && g_pad_handle_owned && g_pad_close) g_pad_close(g_pad_handle);
    g_pad_handle = -1;
    g_pad_handle_owned = 0;
    pthread_mutex_unlock(&g_pad_lock);
}

#ifdef AKENO_EXPERIMENTAL_AUDIO
/* ---- Experimental audio (audio lab builds only) ------------------------------------------- */

/*
 * Declarations and constants as in the PS5 audio driver of ps5-payload-dev/SDL
 * (src/audio/ps5/SDL_ps5audio.h, commit ee4c47d), which plays audio from homebrew payloads.
 * They are not linked: like sceNotificationSend they are resolved with dlopen() only when the
 * user starts the test, so a failure cannot affect the start of the app.
 *
 * Differences to audio lab v1 (whose tone was accepted but not heard on a PS5 with 12.20):
 * the block handed to the system is a 64-byte aligned static buffer instead of a stack array, the
 * format and frames per call follow the SDL driver's common path (S16 stereo, 1024) unless the
 * user picks another verified combination, silence is written before and after the tone, and
 * every call is timed so the report shows whether the system consumed the samples in real time.
 * Nothing else is called: no volume, routing, port state or focus function (no verified PS5
 * declaration exists for those; audio_lab.c only looks their names up).
 */
#include "audio_lab_pcm.h"

typedef int32_t (*audio_out_init_fn)(void);
typedef int32_t (*audio_out_open_fn)(int32_t user_id, int32_t type, int32_t index, uint32_t len, uint32_t freq, uint32_t param);
typedef int32_t (*audio_out_output_fn)(int32_t handle, const void *samples);
typedef int32_t (*audio_out_close_fn)(int32_t handle);

const char *akeno_platform_audio_library_dir(void) { return "/system/common/lib"; }

int akeno_platform_audio_lookup(const char *library, const char *const *names, int *found, size_t count, char *err, size_t err_len) {
    for (size_t i = 0; i < count; i++) found[i] = 0;
    void *lib = dlopen(library, RTLD_LAZY);
    if (!lib) {
        const char *why = dlerror();
        snprintf(err, err_len, "dlopen failed: %s", why ? why : "unknown error");
        return -1;
    }
    /* Looked up only: the addresses are compared with NULL and never called. */
    for (size_t i = 0; i < count; i++) found[i] = dlsym(lib, names[i]) != NULL;
    dlclose(lib);
    return 0;
}

/* Loaded once, on the first test, and kept for the life of the process (the module is
 * initialized with sceAudioOutInit, which the SDL driver also calls only once). */
static void *g_audio_out_lib = NULL;
static audio_out_open_fn g_audio_out_open;
static audio_out_output_fn g_audio_out_output;
static audio_out_close_fn g_audio_out_close;
static int g_audio_init_done = 0;
static int g_audio_init_rc = 0;
static unsigned char g_audio_block[AUDIO_LAB_MAX_BLOCK_BYTES] __attribute__((aligned(64)));
/* Own lock (not g_lock): a test blocks for seconds and must not hold up notifications or the browser. */
static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;

static long long monotonic_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/* g_audio_lock held. Returns 0, -1 (init failed) or -2 (not available). */
static int audio_out_load(akeno_audio_result *r) {
    if (!g_audio_out_lib) {
        void *lib = dlopen("libSceAudioOut.sprx", RTLD_LAZY);
        if (!lib) {
            const char *why = dlerror();
            snprintf(r->error, sizeof(r->error), "dlopen(libSceAudioOut.sprx) failed: %s", why ? why : "unknown error");
            return -2;
        }
        audio_out_init_fn init = (audio_out_init_fn)dlsym(lib, "sceAudioOutInit");
        audio_out_open_fn open_fn = (audio_out_open_fn)dlsym(lib, "sceAudioOutOpen");
        audio_out_output_fn output = (audio_out_output_fn)dlsym(lib, "sceAudioOutOutput");
        audio_out_close_fn close_fn = (audio_out_close_fn)dlsym(lib, "sceAudioOutClose");
        if (!init || !open_fn || !output || !close_fn) {
            snprintf(r->error, sizeof(r->error), "libSceAudioOut.sprx does not export the expected functions");
            dlclose(lib);
            return -2;
        }
        g_audio_out_lib = lib;
        g_audio_out_open = open_fn;
        g_audio_out_output = output;
        g_audio_out_close = close_fn;
        r->init_called = 1;
        g_audio_init_rc = init();
        g_audio_init_done = g_audio_init_rc == 0;
    }
    r->library_loaded = 1;
    r->init_rc = g_audio_init_rc;
    if (!g_audio_init_done) {
        snprintf(r->error, sizeof(r->error), "sceAudioOutInit returned 0x%08x", (unsigned)g_audio_init_rc);
        return -1;
    }
    return 0;
}

int akeno_platform_audio_run(const akeno_audio_params *p, akeno_audio_result *r) {
    memset(r, 0, sizeof(*r));
    r->call_min_us = -1;
    unsigned frame_bytes = audio_lab_format_frame_bytes(p->format);
    if (!frame_bytes || !audio_lab_grain_valid(p->grain) || p->rate != AUDIO_LAB_RATE || p->user_id != 0xFF || p->port_type != 0) {
        snprintf(r->error, sizeof(r->error), "Parameters outside the verified SDL driver configuration");
        return -1;
    }
    r->buffer_aligned = ((uintptr_t)g_audio_block & 63u) == 0;
    pthread_mutex_lock(&g_audio_lock); /* Also keeps two tests from ever sharing the static block. */
    int loaded = audio_out_load(r);
    if (loaded != 0) {
        pthread_mutex_unlock(&g_audio_lock);
        return loaded;
    }
    r->open_called = 1;
    int32_t handle = g_audio_out_open(p->user_id, p->port_type, 0, p->grain, p->rate, (uint32_t)p->format);
    r->open_rc = handle;
    if (handle < 1) {
        snprintf(r->error, sizeof(r->error), "sceAudioOutOpen returned 0x%08x", (unsigned)handle);
        pthread_mutex_unlock(&g_audio_lock);
        return -1;
    }
    int result = 0;
    long long first_start = 0, last_end = 0;
    for (unsigned long done = 0; done < p->frames; done += p->grain) {
        if (p->cancel && *p->cancel) {
            r->cancelled = 1;
            break;
        }
        double peak = 0.0;
        unsigned long nonzero = p->fill(p->fill_user, g_audio_block, p->grain, &peak);
        long long t0 = monotonic_us();
        if (!first_start) first_start = t0;
        int32_t rc = g_audio_out_output(handle, g_audio_block); /* blocks until the system takes the samples */
        long long t1 = monotonic_us();
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
        if (r->calls > 1) { /* The first call returns at once (empty queue); it says nothing about pacing. */
            long long us = t1 - t0;
            r->call_total_us += us;
            if (r->call_min_us < 0 || us < r->call_min_us) r->call_min_us = us;
            if (us > r->call_max_us) r->call_max_us = us;
        }
    }
    r->wall_us = first_start ? last_end - first_start : 0;
    r->close_called = 1;
    r->close_rc = g_audio_out_close(handle);
    if (r->close_rc != 0 && result == 0) {
        snprintf(r->error, sizeof(r->error), "sceAudioOutClose returned 0x%08x", (unsigned)r->close_rc);
        result = -1;
    }
    if (r->call_min_us < 0) r->call_min_us = 0;
    pthread_mutex_unlock(&g_audio_lock);
    return result;
}
#endif
