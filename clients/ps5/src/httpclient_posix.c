/* POSIX/OpenSSL HTTP transport for Linux. The console version will use the
 * PS5 system networking stack with verified TLS/hostname checking. */
#include "httpclient.h"

#include <string.h>

#include "client_internal.h"
#include "http.h"
#include "transport.h"

akeno_status akeno_http_backend_exchange(akeno_client *client, const char *method, const char *path,
                                        const char *json_body, int with_token, int *http_status,
                                        akeno_buf *body_out) {
    if (!client || !method || !path || !http_status || !body_out) return AKENO_ERR_ARGUMENT;

    char host[300];
    if (akeno_build_host_header(client, host, sizeof(host)) != 0) return AKENO_ERR_ARGUMENT;
    int has_body = json_body != NULL || strcmp(method, "POST") == 0 ||
                   strcmp(method, "PUT") == 0 || strcmp(method, "PATCH") == 0;
    const char *body = json_body ? json_body : "{}";

    akeno_buf request;
    akeno_buf_init(&request, 128 * 1024);
    int rc = akeno_buf_printf(&request,
                              "%s %s/api/v1%s HTTP/1.1\r\nHost: %s\r\nUser-Agent: akeno-device-client/" AKENO_CLIENT_VERSION
                              "\r\nAccept: application/json\r\nConnection: close\r\n",
                              method, client->url.base_path, path, host);
    if (rc == 0 && with_token) rc = akeno_buf_printf(&request, "Authorization: Bearer %s\r\n", client->token);
    if (rc == 0 && has_body)
        rc = akeno_buf_printf(&request, "Content-Type: application/json\r\nContent-Length: %zu\r\n", strlen(body));
    if (rc == 0) rc = akeno_buf_append_str(&request, "\r\n");
    if (rc == 0 && has_body) rc = akeno_buf_append_str(&request, body);
    if (rc != 0) {
        akeno_buf_free(&request);
        return AKENO_ERR_TOO_LARGE;
    }

    char err[256] = "";
    akeno_conn *conn = NULL;
    akeno_status status = akeno_conn_open(&client->url, client->ca_file,
                                          client->connect_timeout_ms, &conn, err, sizeof(err));
    if (status != AKENO_OK) {
        akeno_log(client, AKENO_LOG_WARN, "http.connect_failed", "%s", err);
        akeno_buf_free(&request);
        return status;
    }
    akeno_http_head head;
    status = akeno_http_exchange(conn, &request, client->io_timeout_ms, &head, body_out);
    akeno_conn_close(conn);
    akeno_buf_free(&request);
    if (status != AKENO_OK) {
        akeno_log(client, AKENO_LOG_WARN, "http.exchange_failed",
                  "%s %s failed: %s", method, path, akeno_status_name(status));
        return status;
    }

    *http_status = head.status;
    return AKENO_OK;
}
