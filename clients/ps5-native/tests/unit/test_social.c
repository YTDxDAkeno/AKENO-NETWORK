#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "account.h"
#include "akeno/json.h"
#include "ipc.h"
#include "social.h"
#include "test.h"

#define NOW 1792195200LL /* 2026-10-17T00:00:00Z */

/* Answers in the daemon's proxy envelope, shaped like the server's FriendDto / PublicProfile. */
static const char FRIENDS[] =
    "{\"ok\":true,\"status\":200,\"data\":{\"friends\":["
    "{\"id\":\"u-carol\",\"onlineId\":\"carol\",\"displayName\":\"Carol\",\"avatar\":\"a1\",\"avatarImage\":null,\"deleted\":false,"
    "\"friendsSince\":\"2026-01-01T00:00:00Z\",\"statusMessage\":\"\","
    "\"presence\":{\"status\":\"offline\",\"lastSeenAt\":\"2026-10-16T21:00:00.000Z\",\"activity\":null,\"game\":null}},"
    "{\"id\":\"u-bob\",\"onlineId\":\"bob\",\"displayName\":\"Bob\",\"avatar\":\"a2\",\"avatarImage\":null,\"deleted\":false,"
    "\"friendsSince\":\"2026-01-01T00:00:00Z\",\"statusMessage\":\"Ranked tonight\","
    "\"presence\":{\"status\":\"online\",\"lastSeenAt\":null,\"activity\":null,"
    "\"game\":{\"titleId\":\"PPSA01234\",\"name\":\"Astro Bot\",\"platform\":\"PS5\",\"startedAt\":\"2026-10-16T23:30:00Z\",\"art\":null}}},"
    "{\"id\":\"u-ada\",\"onlineId\":\"ada\",\"displayName\":\"Ada Lovelace\",\"avatar\":\"a3\",\"avatarImage\":null,\"deleted\":false,"
    "\"friendsSince\":\"2026-01-01T00:00:00Z\",\"statusMessage\":\"\","
    "\"presence\":{\"status\":\"online\",\"lastSeenAt\":null,\"activity\":\"Composing\",\"game\":null}},"
    "{\"id\":\"u-dan\",\"onlineId\":\"dan\",\"displayName\":\"dan\",\"avatar\":\"a4\",\"avatarImage\":null,\"deleted\":false,"
    "\"friendsSince\":\"2026-01-01T00:00:00Z\",\"statusMessage\":\"\","
    "\"presence\":{\"status\":\"away\",\"lastSeenAt\":null,\"activity\":null,\"game\":null}}"
    "]}}";

static const char PROFILE[] =
    "{\"ok\":true,\"status\":200,\"data\":{"
    "\"user\":{\"id\":\"u-bob\",\"onlineId\":\"bob\",\"displayName\":\"Bob\",\"avatar\":\"a2\",\"avatarImage\":null,\"deleted\":false},"
    "\"presence\":{\"status\":\"online\",\"lastSeenAt\":null,\"activity\":null,\"game\":null},"
    "\"joinedAt\":\"2026-03-01T10:00:00Z\",\"restricted\":false,\"bio\":\"Speedrunner.\",\"statusMessage\":\"Ranked tonight\","
    "\"favoriteGames\":[\"Astro Bot\",\"\",\"Gran Turismo 7\"],\"favoriteGameDetails\":[],"
    "\"relationship\":{\"state\":\"friend\",\"friendRequestId\":null,\"canMessage\":true,\"canSendFriendRequest\":false},"
    "\"mutualFriendCount\":2,\"mutualFriends\":[],\"appearance\":{\"theme\":\"default\"},"
    "\"badges\":[{\"id\":\"early\",\"grantedAt\":null},{\"id\":\"admin\",\"grantedAt\":null}],\"featuredBadges\":[],"
    "\"party\":{\"id\":\"p1\",\"name\":\"Friday crew\",\"memberCount\":3,\"maxMembers\":8,\"isMember\":false}}}";

static const char ACTIVITY[] =
    "{\"ok\":true,\"status\":200,\"data\":{\"current\":null,\"historyVisible\":true,\"playtimeVisible\":false,"
    "\"recentGames\":[{\"titleId\":\"PPSA01234\",\"name\":\"Astro Bot\",\"platform\":\"PS5\",\"art\":null,"
    "\"lastPlayedAt\":\"2026-10-16T22:00:00Z\",\"lastSessionSeconds\":null,\"totalSeconds\":null,\"sessionCount\":4,\"playingNow\":false},"
    "{\"titleId\":\"\",\"name\":\"broken\"}],"
    "\"recentSessions\":[],\"trackedSince\":\"2026-01-01T00:00:00Z\",\"totals\":{\"seconds\":null,\"games\":1,\"sessions\":4}}}";

static void test_proxy_and_friends(void) {
    akn_proxy_result res;
    const akeno_json *data;
    akeno_json_doc *doc = akn_proxy_parse(FRIENDS, strlen(FRIENDS), &res, &data);
    CHECK(doc && res.ok && res.status == 200 && data);
    akn_friend_list *list = calloc(1, sizeof(*list));
    CHECK(akn_fill_friends(data, list) == 0);
    akeno_json_free(doc);
    CHECK(list->count == 4 && !list->truncated);
    /* online + playing first, then online by name, away, offline */
    CHECK_STR(list->items[0].online_id, "bob");
    CHECK_STR(list->items[1].online_id, "ada");
    CHECK_STR(list->items[2].online_id, "dan");
    CHECK_STR(list->items[3].online_id, "carol");
    CHECK(list->online == 3 && list->playing == 1);
    CHECK(list->items[0].presence.game.present);
    CHECK_STR(list->items[0].presence.game.name, "Astro Bot");
    CHECK(list->items[0].presence.game.started_at == NOW - 1800);
    CHECK_STR(list->items[0].status_message, "Ranked tonight");
    CHECK(list->items[3].presence.status == AKN_PRES_OFFLINE && list->items[3].presence.last_seen == NOW - 3 * 3600);
    CHECK(akn_friends_find(list, "u-dan") == 2 && akn_friends_find(list, "nobody") == -1 && akn_friends_find(list, "") == -1);

    const char *req = "{\"ok\":true,\"status\":200,\"data\":{\"incoming\":[{\"id\":\"r1\"},{\"id\":\"r2\"}],\"outgoing\":[]}}";
    doc = akn_proxy_parse(req, strlen(req), &res, &data);
    CHECK(akn_fill_requests(data, list) == 0 && list->incoming_requests == 2 && list->outgoing_requests == 0);
    akeno_json_free(doc);

    /* Errors: the daemon's own, the server's (through the proxy), garbage, and a wrong shape. */
    const char *unpaired = "{\"ok\":false,\"error\":{\"code\":\"NOT_PAIRED\",\"message\":\"This console is not paired.\"}}";
    doc = akn_proxy_parse(unpaired, strlen(unpaired), &res, &data);
    CHECK(doc && !res.ok && !data && res.status == 0);
    CHECK_STR(res.code, "NOT_PAIRED");
    akeno_json_free(doc);
    const char *server = "{\"ok\":false,\"status\":404,\"error\":{\"code\":\"NOT_FOUND\",\"message\":\"User not found\"}}";
    doc = akn_proxy_parse(server, strlen(server), &res, &data);
    CHECK(doc && !res.ok && res.status == 404 && !data);
    CHECK_STR(res.message, "User not found");
    akeno_json_free(doc);
    CHECK(akn_proxy_parse("<html>", 6, &res, &data) == NULL && !res.ok);
    const char *nodata = "{\"ok\":true,\"status\":200}";
    doc = akn_proxy_parse(nodata, strlen(nodata), &res, &data);
    CHECK(doc && !res.ok && !data);
    akeno_json_free(doc);
    const char *shape = "{\"ok\":true,\"status\":200,\"data\":{\"friends\":{}}}";
    doc = akn_proxy_parse(shape, strlen(shape), &res, &data);
    CHECK(akn_fill_friends(data, list) == -1);
    CHECK(akn_fill_requests(data, list) == -1);
    akeno_json_free(doc);
    free(list);
}

static void test_profile_and_activity(void) {
    akn_proxy_result res;
    const akeno_json *data;
    akn_profile p;
    akeno_json_doc *doc = akn_proxy_parse(PROFILE, strlen(PROFILE), &res, &data);
    CHECK(akn_fill_profile(data, &p) == 0);
    akeno_json_free(doc);
    CHECK_STR(p.id, "u-bob");
    CHECK_STR(p.display_name, "Bob");
    CHECK_STR(p.bio, "Speedrunner.");
    CHECK_STR(p.relationship, "friend");
    CHECK(p.favorite_count == 2); /* the empty name is skipped */
    CHECK_STR(p.favorites[1], "Gran Turismo 7");
    CHECK(p.mutual_friends == 2 && p.badges == 2 && !p.restricted);
    CHECK(p.has_party && p.party_members == 3 && p.party_max == 8);
    CHECK_STR(p.party_name, "Friday crew");
    CHECK(p.joined_at > 0 && p.presence.status == AKN_PRES_ONLINE);
    const char *bad = "{\"ok\":true,\"status\":200,\"data\":{\"user\":{\"id\":\"x\"}}}";
    doc = akn_proxy_parse(bad, strlen(bad), &res, &data);
    CHECK(akn_fill_profile(data, &p) == -1);
    akeno_json_free(doc);
    /* A restricted profile: null details stay empty. */
    const char *restricted = "{\"ok\":true,\"status\":200,\"data\":{\"user\":{\"id\":\"u-x\",\"onlineId\":\"x\",\"displayName\":\"\"},"
                             "\"restricted\":true,\"bio\":null,\"statusMessage\":null,\"party\":null,\"presence\":null}}";
    doc = akn_proxy_parse(restricted, strlen(restricted), &res, &data);
    CHECK(akn_fill_profile(data, &p) == 0 && p.restricted && !p.bio[0] && !p.has_party);
    CHECK_STR(p.display_name, "x"); /* falls back to the Online ID */
    akeno_json_free(doc);

    akn_activity a;
    doc = akn_proxy_parse(ACTIVITY, strlen(ACTIVITY), &res, &data);
    CHECK(akn_fill_activity(data, &a) == 0);
    akeno_json_free(doc);
    CHECK(a.history_visible && !a.playtime_visible && !a.current.present);
    CHECK(a.recent_count == 1); /* the entry without a title ID is skipped */
    CHECK(a.recent[0].total_seconds == -1 && a.recent[0].sessions == 4);
    CHECK(a.total_seconds == -1 && a.total_games == 1 && a.total_sessions == 4);
    CHECK(akn_fill_activity(NULL, &a) == -1);
}

static void test_daemon_answers(void) {
    akn_daemon_info d;
    memset(&d, 0, sizeof(d));
    const char *hello = "{\"ok\":true,\"ipc\":1,\"app\":\"akeno-network-console\",\"version\":\"0.5.0-alpha\",\"build\":\"abc-ps5-native-ipc\","
                        "\"paired\":true,\"onlineId\":\"me\",\"realtime\":\"connected\",\"eventsNext\":42}";
    CHECK(akn_parse_daemon_hello(hello, strlen(hello), &d) == 0);
    CHECK(d.ipc == 1 && d.paired && d.events_next == 42);
    CHECK_STR(d.online_id, "me");
    CHECK_STR(d.build, "abc-ps5-native-ipc");
    const char *no_ipc = "{\"ok\":true,\"app\":\"akeno-network-console\",\"version\":\"0.5.0-alpha\"}";
    CHECK(akn_parse_daemon_hello(no_ipc, strlen(no_ipc), &d) == -1);
    const char *other = "{\"ok\":true,\"ipc\":1,\"app\":\"websrv\"}";
    CHECK(akn_parse_daemon_hello(other, strlen(other), &d) == -1);
    CHECK(akn_parse_daemon_hello("nope", 4, &d) == -1);

    const char *state = "{\"ok\":true,\"app\":{\"version\":\"0.5.0-alpha\"},\"configured\":true,\"paired\":true,\"onlineId\":\"me\","
                        "\"realtime\":{\"state\":\"connected\",\"detail\":\"ok\",\"secondsInState\":3},"
                        "\"activity\":{\"enabled\":true,\"state\":\"playing\",\"detail\":\"\",\"titleId\":\"CUSA00001\",\"name\":null,"
                        "\"startedAt\":\"2026-10-16T23:00:00Z\",\"sinceSeconds\":3600},"
                        "\"inbox\":{\"latestSeq\":3,\"seenSeq\":1,\"unseen\":2,\"latest\":null},\"eventsNext\":50}";
    CHECK(akn_parse_daemon_state(state, strlen(state), &d) == 0);
    CHECK_STR(d.realtime, "connected");
    CHECK(d.activity_enabled && d.console_game.present && d.console_game_seconds == 3600 && d.inbox_unseen == 2);
    CHECK_STR(akn_game_label(&d.console_game), "CUSA00001"); /* no name: the title ID */
    CHECK(d.ipc == 1); /* kept from the hello */
    CHECK(akn_parse_daemon_state("{\"ok\":false}", 12, &d) == -1);
}

static void test_events(void) {
    akn_proxy_result res;
    const akeno_json *data;
    akn_friend_list *list = calloc(1, sizeof(*list));
    akeno_json_doc *doc = akn_proxy_parse(FRIENDS, strlen(FRIENDS), &res, &data);
    akn_fill_friends(data, list);
    akeno_json_free(doc);
    akn_profile profile;
    memset(&profile, 0, sizeof(profile));
    snprintf(profile.id, sizeof(profile.id), "u-carol");

    /* Carol comes online and starts a game: applied in place, list re-sorted. */
    const char *ev = "{\"ok\":true,\"next\":12,\"gap\":false,\"events\":["
                     "{\"seq\":11,\"type\":\"presence.updated\",\"data\":{\"userId\":\"u-carol\",\"presence\":{\"status\":\"online\","
                     "\"lastSeenAt\":null,\"activity\":null,\"game\":{\"titleId\":\"PPSA09999\",\"name\":\"Zeta\",\"platform\":\"PS5\","
                     "\"startedAt\":\"2026-10-17T00:00:00Z\"}}}},"
                     "{\"seq\":12,\"type\":\"profile.changed\",\"data\":{\"userId\":\"u-carol\"}}]}";
    akn_events_result r;
    memset(&r, 0, sizeof(r));
    r.next = 10;
    CHECK(akn_apply_events(ev, strlen(ev), list, &profile, &r) == 0);
    CHECK(r.next == 12 && r.events == 2 && r.presence_applied == 1);
    CHECK(r.flags & AKN_EVF_PRESENCE);
    CHECK((r.flags & AKN_EVF_PROFILE) && (r.flags & AKN_EVF_FRIENDS));
    CHECK(!(r.flags & AKN_EVF_GAP));
    CHECK(profile.presence.status == AKN_PRES_ONLINE && profile.presence.game.present);
    CHECK(list->playing == 2 && list->online == 4);
    CHECK(akn_friends_find(list, "u-carol") < 2); /* moved up among the players */

    /* Everything else becomes reload flags; a presence event without data asks for a reload. */
    const char *ev2 = "{\"ok\":true,\"next\":20,\"gap\":true,\"events\":["
                      "{\"seq\":13,\"type\":\"friends.changed\",\"data\":{\"reason\":\"request_received\",\"userId\":\"u-zed\"}},"
                      "{\"seq\":14,\"type\":\"activity.local\",\"data\":{}},"
                      "{\"seq\":15,\"type\":\"message.created\",\"data\":null},"
                      "{\"seq\":16,\"type\":\"party.changed\",\"data\":{\"partyId\":\"p\",\"removed\":false}},"
                      "{\"seq\":17,\"type\":\"invitations.changed\",\"data\":{}},"
                      "{\"seq\":18,\"type\":\"session.revoked\",\"data\":{\"reason\":\"x\"}},"
                      "{\"seq\":19,\"type\":\"presence.updated\",\"data\":null},"
                      "{\"seq\":20,\"type\":\"quickaccess.detected\",\"data\":{\"test\":true}}]}";
    CHECK(akn_apply_events(ev2, strlen(ev2), list, NULL, &r) == 0);
    CHECK(r.next == 20 && r.events == 8 && r.presence_applied == 0);
    unsigned want = AKN_EVF_FRIENDS | AKN_EVF_ACTIVITY | AKN_EVF_STATE | AKN_EVF_MESSAGES | AKN_EVF_PARTY | AKN_EVF_INVITATIONS |
                    AKN_EVF_REVOKED | AKN_EVF_GAP;
    CHECK((r.flags & want) == want);

    /* The daemon restarted: its counter went back. */
    const char *restart = "{\"ok\":true,\"next\":2,\"gap\":false,\"events\":[]}";
    CHECK(akn_apply_events(restart, strlen(restart), list, NULL, &r) == 0);
    CHECK(r.next == 2 && (r.flags & AKN_EVF_GAP));
    /* Malformed answers change nothing. */
    CHECK(akn_apply_events("{\"ok\":true}", 11, list, NULL, &r) == -1);
    CHECK(r.next == 2);
    CHECK(akn_apply_events("[]", 2, NULL, NULL, &r) == -1);
    free(list);
}

static void test_text(void) {
    char out[96];
    akn_presence_view p;
    memset(&p, 0, sizeof(p));
    akn_presence_line(&p, NOW, out, sizeof(out));
    CHECK_STR(out, "Offline");
    p.last_seen = NOW - 7200;
    akn_presence_line(&p, NOW, out, sizeof(out));
    CHECK_STR(out, "Offline \xC2\xB7 last seen 2 h ago");
    p.status = AKN_PRES_AWAY;
    snprintf(p.activity, sizeof(p.activity), "AFK");
    akn_presence_line(&p, NOW, out, sizeof(out));
    CHECK_STR(out, "Away \xC2\xB7 AFK");
    p.game.present = 1;
    snprintf(p.game.title_id, sizeof(p.game.title_id), "PPSA01234");
    akn_presence_line(&p, NOW, out, sizeof(out));
    CHECK_STR(out, "Playing PPSA01234");
    CHECK_STR(akn_presence_label(AKN_PRES_DND), "Do not disturb");

    akn_format_duration(59, out, sizeof(out));
    CHECK_STR(out, "under a minute");
    akn_format_duration(754, out, sizeof(out));
    CHECK_STR(out, "12 min");
    akn_format_duration(7500, out, sizeof(out));
    CHECK_STR(out, "2 h 05 min");
    akn_format_ago(NOW - 10, NOW, out, sizeof(out));
    CHECK_STR(out, "just now");
    akn_format_ago(NOW - 300, NOW, out, sizeof(out));
    CHECK_STR(out, "5 min ago");
    akn_format_ago(NOW - 86400, NOW, out, sizeof(out));
    CHECK_STR(out, "1 day ago");
    akn_format_ago(NOW - 3 * 86400, NOW, out, sizeof(out));
    CHECK_STR(out, "3 days ago");
    akn_format_ago(1788220800LL, NOW, out, sizeof(out)); /* 2026-09-01 */
    CHECK_STR(out, "on 2026-09-01");
    akn_format_ago(0, NOW, out, sizeof(out));
    CHECK_STR(out, "a while ago");

    char ini[8];
    akn_initials("Ada Lovelace", "ada", ini);
    CHECK_STR(ini, "AL");
    akn_initials("", "it_bob", ini);
    CHECK_STR(ini, "I");
    akn_initials("  émile zola", "x", ini);
    CHECK_STR(ini, "\xC3\xA9Z"); /* UTF-8 kept whole */
    akn_initials(NULL, NULL, ini);
    CHECK_STR(ini, "?");
}

/* ---- The native key file --------------------------------------------------------------------------- */

static void write_file(const char *path, const char *text, mode_t mode) {
    unlink(path);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd >= 0) {
        if (write(fd, text, strlen(text)) < 0) perror("write");
        close(fd);
    }
    chmod(path, mode);
}

static void test_native_key(void) {
    char dir[] = "/tmp/akn-key-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[96], link[96], key[AKN_NATIVE_KEY_LEN + 1];
    snprintf(path, sizeof(path), "%s/native.key", dir);
    snprintf(link, sizeof(link), "%s/link.key", dir);
    const char *good = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    CHECK(akn_read_native_key(path, key, sizeof(key)) == -1); /* missing */
    write_file(path, good, 0600);
    CHECK(akn_read_native_key(path, key, sizeof(key)) == 0);
    CHECK_STR(key, good);
    CHECK(akn_read_native_key(path, key, 10) == -2); /* no room */
    write_file(path, good, 0644);
    CHECK(akn_read_native_key(path, key, sizeof(key)) == -2 && key[0] == '\0'); /* readable by others */
    write_file(path, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg", 0600);
    CHECK(akn_read_native_key(path, key, sizeof(key)) == -2); /* not hex */
    write_file(path, "0123456789abcdef", 0600);
    CHECK(akn_read_native_key(path, key, sizeof(key)) == -2); /* too short */
    write_file(path, good, 0600);
    CHECK(symlink(path, link) == 0);
    CHECK(akn_read_native_key(link, key, sizeof(key)) == -2); /* a link is never followed */
    unlink(link);
    unlink(path);
    CHECK(mkdir(path, 0700) == 0);
    CHECK(akn_read_native_key(path, key, sizeof(key)) == -2); /* a directory */
    rmdir(path);
    rmdir(dir);
}

/* ---- The loopback client against raw answers ------------------------------------------------------- */

typedef struct {
    int fd, port;
    const char *answer;
    char request[2048];
} raw_server;

static void *raw_serve(void *arg) {
    raw_server *s = arg;
    int c = accept(s->fd, NULL, NULL);
    if (c < 0) return NULL;
    ssize_t n = recv(c, s->request, sizeof(s->request) - 1, 0);
    if (n > 0) s->request[n] = '\0';
    size_t len = strlen(s->answer), sent = 0;
    while (sent < len) {
        ssize_t w = send(c, s->answer + sent, len - sent, 0);
        if (w <= 0) break;
        sent += (size_t)w;
    }
    close(c);
    return NULL;
}

static int raw_get(const char *answer, const char *key, akn_http_reply *reply, char *request, size_t request_len) {
    raw_server s;
    memset(&s, 0, sizeof(s));
    s.answer = answer;
    s.fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t alen = sizeof(a);
    if (bind(s.fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s.fd, 1) != 0) return -1;
    getsockname(s.fd, (struct sockaddr *)&a, &alen);
    s.port = ntohs(a.sin_port);
    pthread_t t;
    pthread_create(&t, NULL, raw_serve, &s);
    int status = akn_loopback_get(s.port, "/api/state", key, 3000, 4096, reply);
    pthread_join(t, NULL);
    close(s.fd);
    if (request) snprintf(request, request_len, "%s", s.request);
    return status;
}

static void test_loopback(void) {
    akn_http_reply r;
    char req[2048];
    const char *key = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    CHECK(raw_get("HTTP/1.1 200 OK\r\nContent-Length: 11\r\nConnection: close\r\n\r\n{\"ok\":true}", key, &r, req, sizeof(req)) == 200);
    CHECK(r.connected && !r.transport_error && r.len == 11);
    CHECK_STR(r.body, "{\"ok\":true}");
    CHECK_CONTAINS(req, "GET /api/state HTTP/1.1\r\n");
    CHECK_CONTAINS(req, "Host: 127.0.0.1:");
    CHECK_CONTAINS(req, "X-Akeno-Native-Key: 0123456789abcdef");
    CHECK(strstr(req, "Origin") == NULL); /* never: the daemon refuses native requests with one */
    akn_http_reply_free(&r);
    /* Without a key, no key header. */
    raw_get("HTTP/1.1 403 Forbidden\r\nContent-Length: 2\r\n\r\n{}", NULL, &r, req, sizeof(req));
    CHECK(r.status == 403 && strstr(req, "X-Akeno-Native-Key") == NULL);
    akn_http_reply_free(&r);
    /* Chunked (Node's default) */
    CHECK(raw_get("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\n{\"a\"\r\n3\r\n:1}\r\n0\r\n\r\n", key, &r, NULL, 0) == 200);
    CHECK_STR(r.body, "{\"a\":1}");
    akn_http_reply_free(&r);
    /* Close-delimited */
    CHECK(raw_get("HTTP/1.0 200 OK\r\n\r\nhello", key, &r, NULL, 0) == 200);
    CHECK_STR(r.body, "hello");
    akn_http_reply_free(&r);
    /* Cut short, broken chunks, not HTTP, too large: transport errors, no body. */
    CHECK(raw_get("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nshort", key, &r, NULL, 0) == 0 && r.transport_error && !r.body && r.connected);
    akn_http_reply_free(&r);
    CHECK(raw_get("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n", key, &r, NULL, 0) == 0 && r.transport_error);
    akn_http_reply_free(&r);
    CHECK(raw_get("SSH-2.0-OpenSSH\r\n", key, &r, NULL, 0) == 0 && r.transport_error && r.connected);
    akn_http_reply_free(&r);
    static char big[16384];
    memset(big, 'x', sizeof(big) - 1);
    memcpy(big, "HTTP/1.1 200 OK\r\n\r\n", 19);
    CHECK(raw_get(big, key, &r, NULL, 0) == 0 && r.transport_error);
    akn_http_reply_free(&r);
    /* Nothing listening */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t alen = sizeof(a);
    bind(fd, (struct sockaddr *)&a, sizeof(a));
    getsockname(fd, (struct sockaddr *)&a, &alen);
    int port = ntohs(a.sin_port);
    close(fd);
    CHECK(akn_loopback_get(port, "/api/instance", NULL, 1000, 4096, &r) == 0 && !r.connected && r.transport_error);
    akn_http_reply_free(&r);
    /* A path must be absolute and short. */
    CHECK(akn_loopback_get(port, "api", NULL, 1000, 4096, &r) == 0 && r.transport_error);
    akn_http_reply_free(&r);
}

void run_social_tests(void);
void run_social_tests(void) {
    test_proxy_and_friends();
    test_profile_and_activity();
    test_daemon_answers();
    test_events();
    test_text();
    test_native_key();
    test_loopback();
}
