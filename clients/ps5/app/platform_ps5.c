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
 *   sceNotificationSend               samples/notify        experimental, opt-in (button notification)
 *   kernel_get_fw_version             include/ps5/kernel.h  read-only query in the SDK runtime
 *   klog_puts                         include/ps5/klog.h    debug log channel of the SDK runtime
 *
 * Game detection never attaches to, injects into or modifies another process.
 */
#include "platform.h"

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
 * Layout from samples/ps/main.c of the pinned SDK. samples/test_privileges declares the same
 * call without app_type (title_id 4 bytes earlier); only the samples/ps layout is used, and the
 * other one is merely counted for diagnostics so a console test can tell which is right.
 */
typedef struct {
    uint32_t app_id;
    uint64_t unknown1;
    uint32_t app_type;
    char title_id[10];
    char unknown2[0x3c];
} app_info_t;

#define SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM 0xFE /* samples/notify */

int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int blocking);
int sceUserServiceInitialize(void *params);
int sceUserServiceTerminate(void);
int sceSystemServiceLaunchWebBrowser(const char *uri, void *params);
int sceKernelGetAppInfo(pid_t pid, app_info_t *info);
int sceNotificationSend(int userId, bool isLogged, const char *payload);

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_user_service = 0;

void akeno_platform_init(akeno_platform_info *info) {
    memset(info, 0, sizeof(*info));
    info->name = "PlayStation 5";
    unsigned int fw = kernel_get_fw_version();
    info->firmware_raw = fw;
    /* 0xMMmm.... in binary-coded decimal, e.g. 0x12200000 = 12.20. */
    if (fw) snprintf(info->firmware, sizeof(info->firmware), "%x.%02x", (fw >> 24) & 0xFF, (fw >> 16) & 0xFF);
    info->notifications = "sceKernelSendNotificationRequest";
    info->browser = "sceSystemServiceLaunchWebBrowser";
    info->game_detection = "sysctl(KERN_PROC_PROC) + sceKernelGetAppInfo (read-only)";
    info->actionable = "sceNotificationSend (InteractiveToastTemplateB, experimental)";
    /* Required before the browser can be launched (as in samples/browser). */
    g_user_service = sceUserServiceInitialize(NULL) == 0;
}

void akeno_platform_cleanup(void) {
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

static int looks_like_title_id(const char *raw) {
    char id[10];
    memcpy(id, raw, 9);
    id[9] = '\0';
    return akeno_is_game_title_id(id);
}

int akeno_platform_list_apps(akeno_proc_info *out, size_t max, size_t *total, char *err, size_t err_len) {
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0 };
    size_t size = 0;
    *total = 0;
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
        /* Zeroed and larger than the documented structure, in case the system writes more. */
        union {
            app_info_t info;
            unsigned char raw[512];
        } app;
        memset(&app, 0, sizeof(app));
        if (sceKernelGetAppInfo(ki->ki_pid, &app.info) != 0) continue;
        o->has_app_info = 1;
        o->app_id = app.info.app_id;
        o->app_type = app.info.app_type;
        size_t len = strnlen(app.info.title_id, sizeof(app.info.title_id));
        snprintf(o->title_id, sizeof(o->title_id), "%.*s", (int)len, app.info.title_id);
        o->alt_title_layout = !akeno_is_game_title_id(o->title_id) && looks_like_title_id((const char *)app.raw + offsetof(app_info_t, app_type));
    }
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
    int rc = sceNotificationSend(SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM, true, payload);
    pthread_mutex_unlock(&g_lock);
    free(payload);
    return rc == 0 ? 0 : -1;
}

void akeno_platform_debug_line(const char *line) {
    klog_puts(line);
}
