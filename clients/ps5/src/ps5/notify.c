/* Native system notifications. Uses the simple, robust plain-text toast from samples/hello_world. */
#include "notify.h"

#include <string.h>

#include "sce.h"

void akeno_ps5_notify(const char *message) {
    notify_request_t req;
    memset(&req, 0, sizeof req);
    strncpy(req.message, message ? message : "", sizeof req.message - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}
