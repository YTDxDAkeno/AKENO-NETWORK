/*
 * PlayStation 5 HTTP backend: the system libSceHttp2 stack, with TLS provided by libSceSsl using
 * the console's own root CA store. Certificate and host-name verification are on by default and
 * never disabled here. The call sequence follows the SDK's own samples/http2_get program.
 */
#include <stdio.h>
#include <string.h>

#include "../client_internal.h"
#include "../httpclient.h"
#include "ps5_platform.h"
#include "sce.h"

/* httpVersion/autoProxy arguments and pool sizes as used by samples/http2_get. */
#define PS5_HTTP_TEMPLATE_VERSION 3
#define PS5_NET_POOL_BYTES (32 * 1024)
#define PS5_SSL_POOL_BYTES (256 * 1024)
#define PS5_HTTP_POOL_BYTES (256 * 1024)
#define PS5_HTTP_MAX_CONNECTIONS 4
#define PS5_READ_CHUNK 8192

static int g_net_mem = -1;
static int g_ssl_ctx = -1;
static int g_http_ctx = -1;
static int g_template = -1;
static int g_ready = 0;

akeno_status akeno_ps5_http_global_init(void) {
    if (g_ready) return AKENO_OK;
    if (sceNetInit() != 0) return AKENO_ERR_NETWORK;
    if ((g_net_mem = sceNetPoolCreate("akeno", PS5_NET_POOL_BYTES, 0)) < 0) goto fail;
    if ((g_ssl_ctx = sceSslInit(PS5_SSL_POOL_BYTES)) < 0) goto fail;
    if ((g_http_ctx = sceHttp2Init(g_net_mem, g_ssl_ctx, PS5_HTTP_POOL_BYTES, PS5_HTTP_MAX_CONNECTIONS)) < 0) goto fail;
    g_template = sceHttp2CreateTemplate(g_http_ctx, "akeno-device-client/" AKENO_CLIENT_VERSION, PS5_HTTP_TEMPLATE_VERSION, 1);
    if (g_template < 0) goto fail;
    /* Require TLS 1.2 or newer for every request made from this template. */
    sceHttp2SetMinSslVersion(g_template, SCE_SSL_VERSION_TLS1_2);
    g_ready = 1;
    return AKENO_OK;

fail:
    akeno_ps5_http_global_fini();
    return AKENO_ERR_NETWORK;
}

void akeno_ps5_http_global_fini(void) {
    if (g_template >= 0) sceHttp2DeleteTemplate(g_template);
    if (g_http_ctx >= 0) sceHttp2Term(g_http_ctx);
    if (g_ssl_ctx >= 0) sceSslTerm(g_ssl_ctx);
    if (g_net_mem >= 0) sceNetPoolDestroy(g_net_mem);
    g_template = g_http_ctx = g_ssl_ctx = g_net_mem = -1;
    g_ready = 0;
}

/* Builds <scheme>://<host>[:port]<base_path>/api/v1<api_path>. Returns 0 on success. */
static int build_url(const akeno_client *client, const char *api_path, char *out, size_t out_len) {
    int bracket = strchr(client->url.host, ':') != NULL;
    int default_port = client->url.port == (client->url.tls ? 443 : 80);
    char port[8] = "";
    if (!default_port) snprintf(port, sizeof(port), ":%d", client->url.port);
    int n = snprintf(out, out_len, "%s://%s%s%s%s%s/api/v1%s", client->url.tls ? "https" : "http", bracket ? "[" : "",
                     client->url.host, bracket ? "]" : "", port, client->url.base_path, api_path);
    return n > 0 && (size_t)n < out_len ? 0 : -1;
}

akeno_status akeno_http_do(akeno_client *client, const char *method, const char *api_path, const char *bearer,
                           const char *json_body, int *http_status, akeno_buf *body_out) {
    *http_status = 0;
    akeno_buf_reset(body_out);
    akeno_status init = akeno_ps5_http_global_init();
    if (init != AKENO_OK) {
        akeno_log(client, AKENO_LOG_ERROR, "http.init_failed", "Could not initialize the PS5 network stack.");
        return init;
    }

    char url[640];
    if (build_url(client, api_path, url, sizeof(url)) != 0) return AKENO_ERR_ARGUMENT;
    size_t body_len = json_body ? strlen(json_body) : 0;

    int req = sceHttp2CreateRequestWithURL(g_template, method, url, body_len);
    if (req < 0) {
        akeno_log(client, AKENO_LOG_WARN, "http.request_failed", "%s %s: could not create the request.", method, api_path);
        return AKENO_ERR_NETWORK;
    }

    akeno_status status = AKENO_ERR_NETWORK;
    uint32_t connect_us = (uint32_t)client->connect_timeout_ms * 1000u;
    uint32_t io_us = (uint32_t)client->io_timeout_ms * 1000u;
    sceHttp2SetConnectTimeOut(req, connect_us);
    sceHttp2SetResolveTimeOut(req, connect_us);
    sceHttp2SetSendTimeOut(req, io_us);
    sceHttp2SetRecvTimeOut(req, io_us);
    sceHttp2AddRequestHeader(req, "Accept", "application/json", SCE_HTTP2_HEADER_OVERWRITE);
    if (bearer) {
        char auth[96];
        snprintf(auth, sizeof(auth), "Bearer %s", bearer);
        sceHttp2AddRequestHeader(req, "Authorization", auth, SCE_HTTP2_HEADER_OVERWRITE);
    }
    if (json_body) {
        sceHttp2AddRequestHeader(req, "Content-Type", "application/json", SCE_HTTP2_HEADER_OVERWRITE);
        sceHttp2SetRequestContentLength(req, body_len);
    }

    if (sceHttp2SendRequest(req, json_body, body_len) != 0) {
        akeno_log(client, AKENO_LOG_WARN, "http.send_failed", "%s %s: send failed.", method, api_path);
        goto done;
    }
    int code = 0;
    if (sceHttp2GetStatusCode(req, &code) != 0 || code < 100 || code > 599) {
        status = AKENO_ERR_PROTOCOL;
        goto done;
    }
    *http_status = code;

    for (;;) {
        char chunk[PS5_READ_CHUNK];
        int n = sceHttp2ReadData(req, chunk, sizeof(chunk));
        if (n < 0) {
            status = AKENO_ERR_NETWORK;
            goto done;
        }
        if (n == 0) break;
        if (akeno_buf_append(body_out, chunk, (size_t)n) != 0) {
            status = AKENO_ERR_TOO_LARGE;
            goto done;
        }
    }
    status = AKENO_OK;

done:
    sceHttp2DeleteRequest(req);
    return status;
}
