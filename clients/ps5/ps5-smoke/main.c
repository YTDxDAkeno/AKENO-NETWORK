/* AKENO NETWORK PS5 toolchain/loader diagnostic.
 *
 * This is NOT the AKENO NETWORK client. It requests a single local system
 * notification and exits. No networking, account credentials, file writes,
 * PSN services or system patches are involved.
 *
 * ABI reference: ps5-payload-dev/sdk samples/hello_world/main.c.
 * Firmware/loader behavior must still be validated on the target PS5.
 */
#include <stddef.h>
#include <stdio.h>

typedef struct {
    unsigned char reserved[45];
    char message[3075];
} akeno_notification_request;

/* Prototype documented by the pinned ps5-payload-sdk hello_world sample. */
int sceKernelSendNotificationRequest(int, akeno_notification_request *, size_t, int);

int main(void) {
    akeno_notification_request request = {0};
    (void)snprintf(request.message, sizeof(request.message),
                   "AKENO NETWORK: PS5 toolchain diagnostic loaded. "
                   "No network connection or system modification was attempted.");
    return sceKernelSendNotificationRequest(0, &request, sizeof(request), 0) == 0 ? 0 : 1;
}
