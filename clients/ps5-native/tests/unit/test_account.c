#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "account.h"
#include "akeno/akeno.h"
#include "net.h"
#include "test.h"

/* Platform functions net.c needs, for the unit test binary. */
uint64_t akn_platform_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
long long akn_platform_unix_time(void) { return 1792195200LL; } /* 2026-10-17T00:00:00Z */

#define TOKEN "akd_0123456789abcdefghijklmnopqrstuvwxyzABCDEFG"

/* ---- A tiny HTTP server answering one fixed response per connection ---------------------------- */

typedef struct {
    int fd, port;
    int connections; /* how many to serve */
    char response[2048];
    char last_request[2048];
} fake_server;

static void *serve(void *arg) {
    fake_server *s = arg;
    for (int i = 0; i < s->connections; i++) {
        int c = accept(s->fd, NULL, NULL);
        if (c < 0) break;
        char buf[2048];
        ssize_t n = recv(c, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            memcpy(s->last_request, buf, (size_t)n + 1);
        }
        size_t len = strlen(s->response), sent = 0;
        while (sent < len) {
            ssize_t w = send(c, s->response + sent, len - sent, 0);
            if (w <= 0) break;
            sent += (size_t)w;
        }
        close(c);
    }
    return NULL;
}

static int start_server(fake_server *s, pthread_t *t, const char *status, const char *body, int connections) {
    memset(s->last_request, 0, sizeof(s->last_request));
    s->fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s->fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s->fd, 4) != 0) return -1;
    socklen_t len = sizeof(a);
    getsockname(s->fd, (struct sockaddr *)&a, &len);
    s->port = ntohs(a.sin_port);
    s->connections = connections;
    snprintf(s->response, sizeof(s->response),
             "HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", status,
             strlen(body), body);
    return pthread_create(t, NULL, serve, s);
}

static void stop_server(fake_server *s, pthread_t t) {
    shutdown(s->fd, SHUT_RDWR);
    close(s->fd);
    pthread_join(t, NULL);
}

static int write_file(const char *path, const char *text, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return -1;
    ssize_t n = write(fd, text, strlen(text));
    close(fd);
    chmod(path, mode);
    return n == (ssize_t)strlen(text) ? 0 : -1;
}

static int read_file(const char *path, char *buf, size_t len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    return 0;
}

/* ---- Parsing ------------------------------------------------------------------------------------ */

static const char *DEVICE_SELF =
    "{\"device\":{\"id\":\"d1\",\"name\":\"PlayStation 5\",\"platform\":\"ps5\",\"clientVersion\":\"0.5.0\","
    "\"scopes\":[\"profile:read\",\"friends:read\",\"realtime\"],\"createdAt\":\"x\",\"lastUsedAt\":\"x\","
    "\"expiresAt\":\"x\",\"ipAddress\":\"203.0.113.9\",\"current\":true},"
    "\"user\":{\"id\":\"u1\",\"onlineId\":\"akeno_tester\",\"displayName\":\"Akeno Tester\"},"
    "\"apiVersion\":1,\"realtimeProtocolVersion\":1,\"serverTime\":\"2026-10-17T00:00:42.123Z\"}";

static void test_parse(void) {
    akn_account_view v;
    memset(&v, 0, sizeof(v));
    CHECK(akn_parse_device_self(DEVICE_SELF, strlen(DEVICE_SELF), 1792195200LL, &v) == 0);
    CHECK_STR(v.online_id, "akeno_tester");
    CHECK_STR(v.display_name, "Akeno Tester");
    CHECK_STR(v.device_name, "PlayStation 5");
    CHECK(v.scope_count == 3 && v.has_realtime_scope);
    CHECK_STR(v.scopes, "profile:read, friends:read, realtime");
    CHECK(v.have_skew && v.clock_skew_s == 42);
    CHECK(akn_parse_device_self("{\"user\":{}}", 11, 0, &v) == -1);
    CHECK(akn_parse_device_self("not json", 8, 0, &v) == -1);

    char version[32];
    const char *inst = "{\"app\":\"akeno-network-console\",\"version\":\"0.5.0-alpha\"}";
    CHECK(akn_parse_daemon_instance(inst, strlen(inst), version, sizeof(version), NULL) == 0);
    CHECK_STR(version, "0.5.0-alpha");
    const char *other = "{\"app\":\"websrv\"}";
    CHECK(akn_parse_daemon_instance(other, strlen(other), version, sizeof(version), NULL) == -1);

    long long t;
    CHECK(akn_parse_iso8601("1970-01-01T00:00:00Z", &t) == 0 && t == 0);
    CHECK(akn_parse_iso8601("2026-10-17T00:00:00.000Z", &t) == 0 && t == 1792195200LL);
    CHECK(akn_parse_iso8601("2000-02-29T12:00:00Z", &t) == 0 && t == 951825600LL);
    CHECK(akn_parse_iso8601("2026-13-01T00:00:00Z", &t) == -1);
    CHECK(akn_parse_iso8601("2026-10-17 00:00:00Z", &t) == -1);
    CHECK(akn_parse_iso8601("2026-10-17T00:00:00+02:00", &t) == -1);
    CHECK(akn_parse_iso8601(NULL, &t) == -1);
    CHECK_STR(akn_account_state_text(AKN_ACC_SIGNED_IN), "Signed in");
    CHECK_STR(akn_daemon_state_text(AKN_DAEMON_OTHER), "Port 8642 is used by another program");
}

static void test_files(void) {
    char dir[] = "/tmp/akn-acc-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char settings[128], cred[128], out[64];
    snprintf(settings, sizeof(settings), "%s/settings.json", dir);
    snprintf(cred, sizeof(cred), "%s/device.json", dir);
    CHECK(akn_read_server_setting(settings, out, sizeof(out)) == -1);
    write_file(settings, "{\"version\":1,\"server\":\"https://akeno.example\"}", 0600);
    CHECK(akn_read_server_setting(settings, out, sizeof(out)) == 0);
    CHECK_STR(out, "https://akeno.example");
    write_file(settings, "{\"version\":1}", 0600);
    CHECK(akn_read_server_setting(settings, out, sizeof(out)) == 0 && out[0] == '\0');
    write_file(settings, "{broken", 0600);
    CHECK(akn_read_server_setting(settings, out, sizeof(out)) == -1);
    char tiny[8];
    write_file(settings, "{\"server\":\"https://a-very-long-host.example\"}", 0600);
    CHECK(akn_read_server_setting(settings, tiny, sizeof(tiny)) == -1);

    akn_account_view v;
    memset(&v, 0, sizeof(v));
    akn_inspect_credential(cred, &v);
    CHECK(!v.credential_present);
    write_file(cred, "{}", 0600);
    akn_inspect_credential(cred, &v);
    CHECK(v.credential_present && v.credential_private);
    chmod(cred, 0644);
    akn_inspect_credential(cred, &v);
    CHECK(v.credential_present && !v.credential_private);
    char link[128];
    snprintf(link, sizeof(link), "%s/link.json", dir);
    CHECK(symlink(cred, link) == 0);
    akn_inspect_credential(link, &v);
    CHECK(!v.credential_present); /* symlinks are not followed */
    unlink(link);
    unlink(cred);
    unlink(settings);
    rmdir(dir);
}

/* ---- The full read-only check against a fake server and a fake daemon --------------------------- */

static void test_check(void) {
    char dir[] = "/tmp/akn-net-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    akn_platform_info p;
    memset(&p, 0, sizeof(p));
    p.name = "test";
    snprintf(p.akeno_dir, sizeof(p.akeno_dir), "%s", dir);
    akn_log log;
    akn_log_open(&log, NULL, 0, akn_platform_now_ms);

    /* Nothing configured, no daemon (a closed port). */
    fake_server closed;
    pthread_t tc;
    CHECK(start_server(&closed, &tc, "200 OK", "{}", 0) == 0);
    p.daemon_port = closed.port;
    stop_server(&closed, tc);
    akn_account_view v;
    memset(&v, 0, sizeof(v));
    akn_net_check(&p, &v, &log);
    CHECK(v.state == AKN_ACC_NO_SERVER);
    CHECK(v.daemon == AKN_DAEMON_NOT_RUNNING);
    CHECK(v.checks == 1);

    /* Daemon and server answer; the credential is used read-only. */
    fake_server daemon, server;
    pthread_t td, ts;
    CHECK(start_server(&daemon, &td, "200 OK", "{\"app\":\"akeno-network-console\",\"version\":\"0.5.0-alpha\"}", 2) == 0);
    CHECK(start_server(&server, &ts, "200 OK", DEVICE_SELF, 1) == 0);
    p.daemon_port = daemon.port;
    char path[160], text[512];
    snprintf(path, sizeof(path), "%s/settings.json", dir);
    snprintf(text, sizeof(text), "{\"version\":1,\"server\":\"http://127.0.0.1:%d\"}", server.port);
    write_file(path, text, 0600);
    akn_net_check(&p, &v, &log);
    CHECK(v.state == AKN_ACC_NOT_PAIRED); /* no credential yet */
    snprintf(path, sizeof(path), "%s/device.json", dir);
    char cred[400];
    snprintf(cred, sizeof(cred), "{\"version\":1,\"server\":\"http://127.0.0.1:%d\",\"accessToken\":\"%s\",\"deviceId\":\"d1\",\"onlineId\":\"akeno_tester\"}\n",
             server.port, TOKEN);
    write_file(path, cred, 0600);
    struct stat before;
    stat(path, &before);
    akn_net_check(&p, &v, &log);
    stop_server(&daemon, td);
    stop_server(&server, ts);
    CHECK(v.daemon == AKN_DAEMON_RUNNING);
    CHECK_STR(v.daemon_version, "0.5.0-alpha");
    CHECK_CONTAINS(daemon.last_request, "GET /api/instance HTTP/1.1");
    CHECK(v.state == AKN_ACC_SIGNED_IN);
    CHECK_STR(v.online_id, "akeno_tester");
    CHECK(v.http_status == 200);
    CHECK_CONTAINS(server.last_request, "GET /api/v1/devices/current HTTP/1.1");
    CHECK_CONTAINS(server.last_request, "Authorization: Bearer " TOKEN);
    CHECK_STR(v.server_desc, "http to loopback");

    /* 401: reported, and the credential file is left exactly as it was. */
    CHECK(start_server(&server, &ts, "401 Unauthorized",
                       "{\"error\":{\"code\":\"UNAUTHENTICATED\",\"message\":\"Sign in again.\"}}", 1) == 0);
    snprintf(path, sizeof(path), "%s/settings.json", dir);
    snprintf(text, sizeof(text), "{\"version\":1,\"server\":\"http://127.0.0.1:%d\"}", server.port);
    write_file(path, text, 0600);
    snprintf(path, sizeof(path), "%s/device.json", dir);
    snprintf(cred, sizeof(cred), "{\"version\":1,\"server\":\"http://127.0.0.1:%d\",\"accessToken\":\"%s\",\"deviceId\":\"d1\",\"onlineId\":\"akeno_tester\"}\n",
             server.port, TOKEN);
    write_file(path, cred, 0600);
    stat(path, &before);
    p.daemon_port = closed.port;
    akn_net_check(&p, &v, &log);
    stop_server(&server, ts);
    CHECK(v.state == AKN_ACC_UNAUTHORIZED);
    CHECK(v.http_status == 401);
    CHECK_STR(v.error_code, "UNAUTHENTICATED");
    struct stat after;
    CHECK(stat(path, &after) == 0);
    CHECK(after.st_size == before.st_size && after.st_mtime == before.st_mtime && after.st_ino == before.st_ino);
    char now_text[400];
    CHECK(read_file(path, now_text, sizeof(now_text)) == 0);
    CHECK_STR(now_text, cred);

    /* The token never reaches the log. */
    char lines[AKN_LOG_RING][AKN_LOG_LINE];
    int n = akn_log_recent(&log, lines, AKN_LOG_RING);
    for (int i = 0; i < n; i++) CHECK(strstr(lines[i], "akd_0123") == NULL);
    akn_log_close(&log);

    unlink(path);
    snprintf(path, sizeof(path), "%s/settings.json", dir);
    unlink(path);
    rmdir(dir);
}

void run_account_tests(void) {
    CHECK(akeno_global_init() == AKENO_OK);
    test_parse();
    test_files();
    test_check();
    akeno_global_cleanup();
}
