/*
 * Prototypes for the PlayStation 5 system functions the AKENO client uses.
 *
 * The ps5-payload SDK ships no headers for these libraries; its own sample programs declare the
 * prototypes inline. These declarations are taken from those samples and from the SDK's stub
 * symbol lists — nothing here is invented. Each group notes the SDK sample it comes from.
 *
 * Sources (ps5-payload-dev/sdk):
 *   samples/http2_get/main.c      libSceNet / libSceSsl / libSceHttp2 usage
 *   samples/notify/main.c         sceNotificationSend
 *   samples/hello_world/main.c    sceKernelSendNotificationRequest
 *   sce_stubs/libSceHttp2.c       sceHttp2AddRequestHeader, SetRequestContentLength,
 *                                 GetResponseContentLength, SetMinSslVersion, timeouts
 *   sce_stubs/libScePad.c         scePad* controller input
 */
#ifndef AKENO_SCE_H
#define AKENO_SCE_H

#include <stddef.h>
#include <stdint.h>

/* --- libSceNet / libSceSsl / libSceHttp2 (samples/http2_get) ------------------------------- */
int sceNetInit(void);
int sceNetTerm(void);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetPoolDestroy(int memid);

int sceSslInit(size_t poolsize);
int sceSslTerm(int ctxid);

int sceHttp2Init(int netMemId, int sslCtxId, size_t poolSize, int maxConnections);
int sceHttp2Term(int ctxId);

int sceHttp2CreateTemplate(int ctxId, const char *userAgent, int httpVersion, int autoProxyConf);
int sceHttp2DeleteTemplate(int tmplId);

int sceHttp2CreateRequestWithURL(int tmplId, const char *method, const char *url, uint64_t contentLength);
int sceHttp2DeleteRequest(int reqId);

/* sce_stubs/libSceHttp2.c */
int sceHttp2AddRequestHeader(int id, const char *name, const char *value, uint32_t mode);
int sceHttp2SetRequestContentLength(int id, uint64_t contentLength);
int sceHttp2SetConnectTimeOut(int id, uint32_t usec);
int sceHttp2SetRecvTimeOut(int id, uint32_t usec);
int sceHttp2SetSendTimeOut(int id, uint32_t usec);
int sceHttp2SetResolveTimeOut(int id, uint32_t usec);
int sceHttp2SetMinSslVersion(int id, int version);

int sceHttp2SendRequest(int reqId, const void *data, size_t size);
int sceHttp2GetStatusCode(int reqId, int *statusCode);
int sceHttp2GetResponseContentLength(int reqId, int *result, uint64_t *contentLength);
int sceHttp2ReadData(int reqId, void *data, size_t size);

/* HTTP header-add mode: overwrite any existing header of the same name. */
#define SCE_HTTP2_HEADER_OVERWRITE 1u
/* Minimum TLS version 1.2 (SCE_SSL_VERSION_TLS1_2). */
#define SCE_SSL_VERSION_TLS1_2 0x304

/* --- Native notifications ------------------------------------------------------------------ */

/* samples/hello_world: simple system toast with a plain-text message. */
typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;
int sceKernelSendNotificationRequest(int device, notify_request_t *req, size_t size, int blocking);

/* samples/notify: richer JSON toast via the notification system service. */
#define SCE_NOTIFICATION_LOCAL_USER_ID_SYSTEM 0xFE
int sceNotificationSend(int userId, int isLogged, const char *payload);

/* --- libScePad controller input (sce_stubs/libScePad) -------------------------------------- */
int scePadInit(void);
int scePadOpen(int userId, int type, int index, void *param);
int scePadClose(int handle);
int scePadRead(int handle, void *data, int num);
int scePadReadState(int handle, void *data);

/* DualSense button bitmask (standard ScePadButtonDataOffset values). */
#define SCE_PAD_BUTTON_UP 0x0010
#define SCE_PAD_BUTTON_RIGHT 0x0020
#define SCE_PAD_BUTTON_DOWN 0x0040
#define SCE_PAD_BUTTON_LEFT 0x0080
#define SCE_PAD_BUTTON_TRIANGLE 0x1000
#define SCE_PAD_BUTTON_CIRCLE 0x2000
#define SCE_PAD_BUTTON_CROSS 0x4000
#define SCE_PAD_BUTTON_SQUARE 0x8000
#define SCE_PAD_BUTTON_OPTIONS 0x0008
#define SCE_PAD_BUTTON_L1 0x0400
#define SCE_PAD_BUTTON_R1 0x0800

/*
 * ScePadData is large (~160 bytes). We only read the leading button bitmask and the connected
 * flag, so we over-allocate a raw buffer and read the two fields at their documented offsets
 * rather than depend on the full struct layout.
 */
#define SCE_PAD_DATA_SIZE 272
#define SCE_PAD_DATA_BUTTONS_OFFSET 0
#define SCE_PAD_DATA_CONNECTED_OFFSET 8

#endif
