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

#ifdef AKENO_EXPERIMENTAL_AUDIO
/* ---- Experimental audio (audio lab builds only) ------------------------------------------- */

/*
 * Declarations and constants as in the PS5 audio driver of ps5-payload-dev/SDL
 * (src/audio/ps5/SDL_ps5audio.h, commit ee4c47d), which plays audio from homebrew payloads.
 * They are not linked: like sceNotificationSend they are resolved with dlopen() only when the
 * user starts the test, so a failure cannot affect the start of the app.
 */
typedef int32_t (*audio_out_init_fn)(void);
typedef int32_t (*audio_out_open_fn)(int32_t user_id, int32_t type, int32_t index, uint32_t len, uint32_t freq, uint32_t param);
typedef int32_t (*audio_out_output_fn)(int32_t handle, const void *samples);
typedef int32_t (*audio_out_close_fn)(int32_t handle);
#define AUDIO_OUT_USER_ID_SYSTEM 0xFF
#define AUDIO_OUT_PORT_TYPE_MAIN 0
#define AUDIO_OUT_FORMAT_S16_MONO 0
#define AUDIO_OUT_FREQUENCY 48000u
#define AUDIO_OUT_GRAIN 256u /* samples per output call; the SDL driver uses 256..2048 in steps of 256 */

const char *akeno_platform_audio_library_dir(void) { return "/system/common/lib"; }

int akeno_platform_audio_lookup(const char *library, const char *const *names, int *found, size_t count, char *err, size_t err_len) {
    for (size_t i = 0; i < count; i++) found[i] = 0;
    void *lib = dlopen(library, RTLD_LAZY);
    if (!lib) {
        const char *why = dlerror();
        snprintf(err, err_len, "dlopen failed: %s", why ? why : "unknown error");
        return -1;
    }
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

static int audio_out_load(char *err, size_t err_len) {
    if (g_audio_out_lib) return 0;
    void *lib = dlopen("libSceAudioOut.sprx", RTLD_LAZY);
    if (!lib) {
        const char *why = dlerror();
        snprintf(err, err_len, "dlopen(libSceAudioOut.sprx) failed: %s", why ? why : "unknown error");
        return -2;
    }
    audio_out_init_fn init = (audio_out_init_fn)dlsym(lib, "sceAudioOutInit");
    g_audio_out_open = (audio_out_open_fn)dlsym(lib, "sceAudioOutOpen");
    g_audio_out_output = (audio_out_output_fn)dlsym(lib, "sceAudioOutOutput");
    g_audio_out_close = (audio_out_close_fn)dlsym(lib, "sceAudioOutClose");
    if (!init || !g_audio_out_open || !g_audio_out_output || !g_audio_out_close) {
        snprintf(err, err_len, "libSceAudioOut.sprx does not export the expected functions");
        dlclose(lib);
        return -2;
    }
    int32_t rc = init();
    if (rc != 0) {
        snprintf(err, err_len, "sceAudioOutInit returned 0x%08x", (unsigned)rc);
        dlclose(lib);
        return -1;
    }
    g_audio_out_lib = lib;
    return 0;
}

int akeno_platform_audio_tone(unsigned frequency_hz, unsigned duration_ms, double amplitude, char *err, size_t err_len) {
    pthread_mutex_lock(&g_lock);
    int loaded = audio_out_load(err, err_len);
    pthread_mutex_unlock(&g_lock);
    if (loaded != 0) return loaded;
    int32_t handle = g_audio_out_open(AUDIO_OUT_USER_ID_SYSTEM, AUDIO_OUT_PORT_TYPE_MAIN, 0, AUDIO_OUT_GRAIN, AUDIO_OUT_FREQUENCY,
                                      AUDIO_OUT_FORMAT_S16_MONO);
    if (handle < 1) {
        snprintf(err, err_len, "sceAudioOutOpen returned 0x%08x", (unsigned)handle);
        return -1;
    }
    int16_t block[AUDIO_OUT_GRAIN];
    const unsigned long total = (unsigned long)duration_ms * AUDIO_OUT_FREQUENCY / 1000u;
    const unsigned long ramp = AUDIO_OUT_FREQUENCY / 100u; /* 10 ms fade in and out: no clicks */
    /*
     * Sine by recurrence, s[n] = 2cos(w)s[n-1] - s[n-2], with cos(w) and sin(w) from short Taylor
     * series (w is about 0.06 rad): no libm import, which a console has not been shown to resolve.
     */
    const double w = 2.0 * 3.14159265358979323846 * frequency_hz / AUDIO_OUT_FREQUENCY, w2 = w * w;
    const double cos_w = 1.0 - w2 / 2.0 + w2 * w2 / 24.0 - w2 * w2 * w2 / 720.0;
    const double sin_w = w - w * w2 / 6.0 + w * w2 * w2 / 120.0;
    double s_prev = -sin_w, s_cur = 0.0; /* s[-1] = sin(-w), s[0] = sin(0) */
    int result = 0;
    for (unsigned long done = 0; done < total; done += AUDIO_OUT_GRAIN) {
        for (unsigned i = 0; i < AUDIO_OUT_GRAIN; i++) {
            unsigned long n = done + i;
            double gain = 0.0;
            if (n < total) {
                unsigned long left = total - n;
                gain = amplitude * (n < ramp ? (double)n / ramp : left < ramp ? (double)left / ramp : 1.0);
            }
            block[i] = (int16_t)(32767.0 * gain * s_cur);
            double s_next = 2.0 * cos_w * s_cur - s_prev;
            s_prev = s_cur;
            s_cur = s_next;
        }
        int32_t rc = g_audio_out_output(handle, block); /* blocks until the device takes the samples */
        if (rc < 0) {
            snprintf(err, err_len, "sceAudioOutOutput returned 0x%08x", (unsigned)rc);
            result = -1;
            break;
        }
    }
    g_audio_out_close(handle);
    return result;
}
#endif

#ifdef AKENO_CAPTURE_DIAG
/*
 * PS5 capture capability diagnostic (docs/PS5_DIRECT_CAPTURE_RESEARCH.md). Read-only. The only
 * run-time loading here is dlopen() in the RESOLVE tier, confined to this platform file and run
 * only after an explicit user action; nothing resolved is ever called. No sce* function beyond
 * the ones the release ELF already imports is referenced, so the import gate stays satisfied.
 */
#include <sys/stat.h>

const char *akeno_platform_capture_library_dir(void) { return "/system/common/lib"; }

int akeno_platform_capture_file_exists(const char *library) {
    const char *dir = akeno_platform_capture_library_dir();
    if (!dir || !library) return 0;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, library);
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) ? 1 : 0;
}

int akeno_platform_capture_process_info_available(void) { return sceKernelGetAppInfo ? 1 : 0; }

static void capture_log(akeno_capture_log_fn log_fn, void *ctx, const char *event, const char *detail) {
    if (log_fn) log_fn(ctx, event, detail ? detail : "");
}

int akeno_platform_capture_probe_one(const char *library, const char *const *names, int *found, size_t count,
                                     char *err, size_t err_len, akeno_capture_log_fn log_fn, void *log_ctx) {
    for (size_t i = 0; i < count; i++) found[i] = 0;
    err[0] = '\0';
    char line[160];

    /*
     * dlopen on this SDK runs the SDK loader (crt/rtld_sprx.c): for a module not already mapped in
     * this process it calls sceKernelLoadStartModule, which loads AND STARTS the module. Starting
     * a capture/record/broadcast system module can have side effects. We log before and after so
     * that, if the console app dies here, akeno.log names the exact library whose load did it.
     */
    snprintf(line, sizeof(line), "begin dlopen %s (loads+starts the module if not already mapped)", library ? library : "");
    capture_log(log_fn, log_ctx, "capture.resolve.dlopen", line);
    void *lib = dlopen(library, RTLD_LAZY);
    if (!lib) {
        const char *why = dlerror();
        snprintf(err, err_len, "dlopen failed: %s", why ? why : "unknown error");
        snprintf(line, sizeof(line), "end dlopen %s FAILED", library ? library : "");
        capture_log(log_fn, log_ctx, "capture.resolve.dlopen", line);
        return -1;
    }
    snprintf(line, sizeof(line), "end dlopen %s ok", library ? library : "");
    capture_log(log_fn, log_ctx, "capture.resolve.dlopen", line);

    for (size_t i = 0; i < count; i++) {
        snprintf(line, sizeof(line), "begin dlsym %s %s", library ? library : "", names[i] ? names[i] : "");
        capture_log(log_fn, log_ctx, "capture.resolve.dlsym", line);
        found[i] = dlsym(lib, names[i]) != NULL;
        snprintf(line, sizeof(line), "end dlsym %s %s %s", library ? library : "", names[i] ? names[i] : "",
                 found[i] ? "resolved" : "absent");
        capture_log(log_fn, log_ctx, "capture.resolve.dlsym", line);
    }

    /*
     * Intentionally NO dlclose: on this SDK dlclose of a freshly loaded module calls
     * sceKernelStopUnloadModule, and stopping/unloading an unverified system module is not known
     * to be safe. For a short-lived, read-only diagnostic, leaving the module mapped is the safe
     * choice; the process exits normally afterwards and the OS reclaims everything.
     */
    snprintf(line, sizeof(line), "%s left loaded (module unload not verified safe; no dlclose)", library ? library : "");
    capture_log(log_fn, log_ctx, "capture.resolve.noclose", line);
    return 0;
}

int akeno_platform_capture_dns_summary(char *out, size_t out_len) {
    /*
     * Not read on the console. Reading a resolver file would add libc imports (fopen/fgets) that
     * the release ELF does not have, and this diagnostic must keep the release's exact import set
     * (the import safety gate). The resolver question (the track-B DNS conflict) is handled in
     * docs/PS5_DIRECT_CAPTURE_RESEARCH.md rather than by touching the console's configuration.
     */
    snprintf(out, out_len, "not read on the console (read-only build keeps the release import set unchanged)");
    return 0;
}
#endif
