/*
 * PS5 platform layer. Each system call below is used exactly as in a sample of the pinned
 * ps5-payload-sdk (commit f5d40f02770e9b0001dd9e18056eca70b3da8b40); no other system
 * interface, offset or patch is used by AKENO code:
 *
 *   sceKernelSendNotificationRequest  samples/hello_world   (verified on a PS5 with 12.20)
 *   sceUserServiceInitialize          samples/browser       (not yet tested on hardware)
 *   sceSystemServiceLaunchWebBrowser  samples/browser       (not yet tested on hardware)
 *   kernel_get_fw_version             include/ps5/kernel.h  read-only query in the SDK runtime
 *   klog_puts                         include/ps5/klog.h    debug log channel of the SDK runtime
 */
#include "platform.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>

/* Layout from samples/hello_world/main.c of the pinned SDK. */
typedef struct {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int blocking);
int sceUserServiceInitialize(void *params);
int sceUserServiceTerminate(void);
int sceSystemServiceLaunchWebBrowser(const char *uri, void *params);

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

void akeno_platform_debug_line(const char *line) {
    klog_puts(line);
}
