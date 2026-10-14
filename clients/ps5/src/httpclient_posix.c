/* Host/Linux HTTP backend: the from-scratch HTTP/1.1 client over the POSIX + OpenSSL transport. */
#include "httpclient.h"

#include <stdio.h>
#include <string.h>

#include "client_internal.h"
#include "http.h"
#include "transport.h"

/* Builds the Host header value, bracketing IPv6 literals and omitting the default port. */
static int build_host_header(const akeno_client *client, char *out, size_t out_len) {
    int bracket = strchr(client->url.host, ':') != NULL;
    int default_port = client->url.port == (client->url.tls ? 443 : 80);
    char port[8] = "";
    if (!default_port) snprintf(port, sizeof(port), ":%d", client->url.port);
    int n = snprintf(out, out_len, "%s%s%s%s", bracket ? "[" : "", client->url.host, bracket ? "]" : "", port);
    return n > 0 && (size_t)n < out_len ? 0 : -1;
}

akeno_status akeno_http_do(akeno_client *client, const char *method, const char *api_path, const char *bearer,
                           const char *json_body, int *http_status, akeno_buf *body_out) {
    *http_status = 0;
    char host[300];
    if (build_host_header(client, host, sizeof(host)) != 0) return AKENO_ERR_ARGUMENT;

    akeno_buf request;
    akeno_buf_init(&request, 128 * 1024);
    int rc = akeno_buf_printf(&request,
                              "%s %s/api/v1%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: akeno-device-client/" AKENO_CLIENT_VERSION
                              "\r\nAccept: application/json\r\nConnection: close\r\n",
                              method, client->url.base_path, api_path, host);
    if (rc == 0 && bearer) rc = akeno_buf_printf(&request, "Authorization: Bearer %s\r\n", bearer);
    if (rc == 0 && json_body)
        rc = akeno_buf_printf(&request, "Content-Type: application/json\r\nContent-Length: %zu\r\n", strlen(json_body));
    if (rc == 0) rc = akeno_buf_append_str(&request, "\r\n");
    if (rc == 0 && json_body) rc = akeno_buf_append_str(&request, json_body);
    if (rc != 0) {
        akeno_buf_free(&request);
        return AKENO_ERR_TOO_LARGE;
    }

    char err[256] = "";
    akeno_conn *conn = NULL;
    akeno_status status = akeno_conn_open(&client->url, client->ca_file, client->connect_timeout_ms, &conn, err, sizeof(err));
    if (status != AKENO_OK) {
        akeno_log(client, AKENO_LOG_WARN, "http.connect_failed", "%s", err);
        akeno_buf_free(&request);
        return status;
    }
    akeno_http_head head;
    status = akeno_http_exchange(conn, &request, client->io_timeout_ms, &head, body_out);
    akeno_conn_close(conn);
    akeno_buf_free(&request);
    if (status == AKENO_OK) *http_status = head.status;
    return status;
}
