/*
 * akeno-cli: command-line front end for the AKENO device client library. It is the reference
 * implementation of the console client logic and is used for automated tests on Linux.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "akeno/akeno.h"
#include "akeno/caps.h"

enum {
    EXIT_USAGE = 2,
    EXIT_NOT_PAIRED = 3,
    EXIT_UNAUTHORIZED = 4,
    EXIT_FORBIDDEN = 5,
    EXIT_NETWORK = 6,
    EXIT_API = 7,
    EXIT_PAIRING = 8,
    EXIT_GAVE_UP = 9,
};

static volatile sig_atomic_t g_stop = 0;
static long long g_stop_at_ms = 0;
static int g_json = 0;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static int should_stop(void *user) {
    (void)user;
    return g_stop || (g_stop_at_ms && now_ms() >= g_stop_at_ms);
}

static akeno_log_level g_level = AKENO_LOG_WARN;

/* Diagnostics go to stderr as one JSON object per line so they can be collected and parsed. */
static void log_line(void *user, akeno_log_level level, const char *event, const char *message) {
    (void)user;
    static const char *const names[] = { "debug", "info", "warn", "error" };
    fprintf(stderr, "{\"level\":\"%s\",\"event\":\"%s\",\"message\":\"", names[level], event);
    for (const char *p = message; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', stderr);
        if ((unsigned char)*p >= 0x20) fputc(*p, stderr);
    }
    fputs("\"}\n", stderr);
}

static int exit_code_for(akeno_status status) {
    switch (status) {
    case AKENO_OK: return 0;
    case AKENO_ERR_ARGUMENT: case AKENO_ERR_INSECURE_URL: return EXIT_USAGE;
    case AKENO_ERR_NOT_PAIRED: return EXIT_NOT_PAIRED;
    case AKENO_ERR_UNAUTHORIZED: return EXIT_UNAUTHORIZED;
    case AKENO_ERR_FORBIDDEN: return EXIT_FORBIDDEN;
    case AKENO_ERR_NETWORK: case AKENO_ERR_TIMEOUT: case AKENO_ERR_TLS: return EXIT_NETWORK;
    case AKENO_ERR_PAIRING_DENIED: case AKENO_ERR_PAIRING_EXPIRED: case AKENO_ERR_PAIRING_INVALID: return EXIT_PAIRING;
    case AKENO_ERR_GAVE_UP: return EXIT_GAVE_UP;
    case AKENO_ERR_STOPPED: return 0;
    default: return EXIT_API;
    }
}

static void print_json(const akeno_json *node, FILE *out);

static void print_string(const char *s, FILE *out) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(out, "\\%c", *p);
        else if (*p == '\n') fputs("\\n", out);
        else if (*p < 0x20) fprintf(out, "\\u%04x", *p);
        else fputc(*p, out);
    }
    fputc('"', out);
}

static void print_json(const akeno_json *node, FILE *out) {
    if (!node) { fputs("null", out); return; }
    switch (node->type) {
    case AKENO_JSON_NULL: fputs("null", out); break;
    case AKENO_JSON_BOOL: fputs(node->boolean ? "true" : "false", out); break;
    case AKENO_JSON_NUMBER: fputs(node->string, out); break;
    case AKENO_JSON_STRING: print_string(node->string, out); break;
    case AKENO_JSON_ARRAY:
    case AKENO_JSON_OBJECT: {
        int object = node->type == AKENO_JSON_OBJECT;
        fputc(object ? '{' : '[', out);
        for (const akeno_json *c = node->child; c; c = c->next) {
            if (object) { print_string(c->key, out); fputc(':', out); }
            print_json(c, out);
            if (c->next) fputc(',', out);
        }
        fputc(object ? '}' : ']', out);
        break;
    }
    }
}

static int report_failure(const char *what, akeno_status status, const akeno_response *response) {
    if (response && response->error_message[0]) {
        fprintf(stderr, "%s failed: %s (%s)\n", what, response->error_message, response->error_code);
    } else {
        fprintf(stderr, "%s failed: %s\n", what, akeno_status_name(status));
    }
    if (status == AKENO_ERR_NOT_PAIRED) fprintf(stderr, "This device is not paired. Run: akeno-cli pair\n");
    if (status == AKENO_ERR_UNAUTHORIZED) fprintf(stderr, "This device was unpaired on the server. Run: akeno-cli pair\n");
    return exit_code_for(status);
}

/* Performs a request and prints the JSON body (--json) or a short summary via the printer. */
static int call(akeno_client *client, const char *what, const char *method, const char *path, const char *body,
                void (*printer)(const akeno_json *)) {
    akeno_response response;
    akeno_status status = akeno_request(client, method, path, body, &response);
    if (status != AKENO_OK) {
        int code = report_failure(what, status, &response);
        if (status == AKENO_ERR_UNAUTHORIZED) akeno_forget_credentials(client);
        akeno_response_free(&response);
        return code;
    }
    const akeno_json *root = akeno_response_json(&response);
    if (g_json || !printer) {
        print_json(root, stdout);
        fputc('\n', stdout);
    } else {
        printer(root);
    }
    akeno_response_free(&response);
    return 0;
}

static const char *presence_of(const akeno_json *user) {
    return akeno_json_string(akeno_json_path(user, "presence.status"), "unknown");
}

static void print_me(const akeno_json *root) {
    printf("%s (%s)\n", akeno_json_get_string(akeno_json_get(root, "profile"), "displayName", "?"),
           akeno_json_get_string(root, "onlineId", "?"));
}

static void print_friends(const akeno_json *root) {
    const akeno_json *list = akeno_json_get(root, "friends");
    if (!list || list->count == 0) { printf("No friends yet.\n"); return; }
    for (const akeno_json *f = list->child; f; f = f->next) {
        const char *activity = akeno_json_string(akeno_json_path(f, "presence.activity"), NULL);
        printf("%-8s %-16s %s%s%s\n", presence_of(f), akeno_json_get_string(f, "onlineId", "?"), akeno_json_get_string(f, "displayName", ""),
               activity ? " - " : "", activity ? activity : "");
    }
}

static void print_conversations(const akeno_json *root) {
    const akeno_json *list = akeno_json_get(root, "conversations");
    if (!list || list->count == 0) { printf("No conversations yet.\n"); return; }
    for (const akeno_json *c = list->child; c; c = c->next) {
        int64_t unread = 0;
        akeno_json_int64(akeno_json_get(c, "unreadCount"), &unread);
        printf("%s  %-16s unread %lld\n", akeno_json_get_string(c, "id", "?"),
               akeno_json_get_string(akeno_json_get(c, "otherUser"), "onlineId", "?"), (long long)unread);
    }
}

static void print_messages(const akeno_json *root) {
    const akeno_json *list = akeno_json_get(root, "messages");
    if (!list || list->count == 0) { printf("No messages.\n"); return; }
    for (const akeno_json *m = list->child; m; m = m->next) {
        printf("[%s] %s: %s\n", akeno_json_get_string(m, "createdAt", "?"),
               akeno_json_get_string(akeno_json_get(m, "sender"), "onlineId", "?"), akeno_json_get_string(m, "body", ""));
    }
}

static void print_invitations(const akeno_json *root) {
    const akeno_json *list = akeno_json_get(root, "invitations");
    if (!list || list->count == 0) { printf("No pending invitations.\n"); return; }
    for (const akeno_json *i = list->child; i; i = i->next) {
        printf("%s  %-12s %-24s from %s\n", akeno_json_get_string(i, "id", "?"), akeno_json_get_string(i, "kind", "?"),
               akeno_json_get_string(akeno_json_get(i, "target"), "name", ""),
               akeno_json_get_string(akeno_json_get(i, "inviter"), "onlineId", "?"));
    }
}

static void print_party(const akeno_json *root) {
    const akeno_json *party = akeno_json_get(root, "party");
    if (!party || party->type != AKENO_JSON_OBJECT) { printf("You are not in a party.\n"); return; }
    const akeno_json *members = akeno_json_get(party, "members");
    printf("Party %s (%zu members, text chat only)\n", akeno_json_get_string(party, "name", ""), members ? members->count : 0);
}

static void print_sessions(const akeno_json *root) {
    const akeno_json *list = akeno_json_get(root, "sessions");
    if (!list || list->count == 0) { printf("No game sessions.\n"); return; }
    for (const akeno_json *s = list->child; s; s = s->next) {
        printf("%s  %-24s %s\n", akeno_json_get_string(s, "id", "?"), akeno_json_get_string(s, "gameName", "?"),
               akeno_json_get_string(s, "status", "?"));
    }
}

static void print_ok(const akeno_json *root) {
    (void)root;
    printf("OK\n");
}

static int valid_id(const char *s) {
    if (!s || strlen(s) > 64 || !*s) return 0;
    for (const char *p = s; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '-' || *p == '_')) return 0;
    return 1;
}

static void random_uuid(char out[37]) {
    unsigned char b[16];
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(b, 1, sizeof(b), f) != sizeof(b)) {
        for (size_t i = 0; i < sizeof(b); i++) b[i] = (unsigned char)rand();
    }
    if (f) fclose(f);
    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40);
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80);
    snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4],
             b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

static int append_json_string(char *out, size_t out_len, const char *s) {
    size_t o = strlen(out);
    if (o + 2 >= out_len) return -1;
    out[o++] = '"';
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char esc[8] = { 0 };
        if (*p == '"' || *p == '\\') snprintf(esc, sizeof(esc), "\\%c", *p);
        else if (*p < 0x20) snprintf(esc, sizeof(esc), "\\u%04x", *p);
        else esc[0] = (char)*p;
        size_t n = strlen(esc);
        if (o + n + 2 >= out_len) return -1;
        memcpy(out + o, esc, n);
        o += n;
    }
    out[o++] = '"';
    out[o] = '\0';
    return 0;
}

/* send <onlineId|conversationId> <text>: resolves Online IDs to a direct conversation first. */
static int cmd_send(akeno_client *client, const char *target, const char *text) {
    if (!valid_id(target) || !text || !*text || strlen(text) > 2000) {
        fprintf(stderr, "Usage: akeno-cli send <onlineId|conversationId> <text up to 2000 characters>\n");
        return EXIT_USAGE;
    }
    char conversation[64];
    snprintf(conversation, sizeof(conversation), "%s", target);
    int is_uuid = strlen(target) == 36 && target[8] == '-' && target[13] == '-';
    if (!is_uuid) {
        char path[96];
        snprintf(path, sizeof(path), "/users/%s", target);
        akeno_response user;
        akeno_status status = akeno_request(client, "GET", path, NULL, &user);
        if (status != AKENO_OK) {
            int code = report_failure("Looking up the user", status, &user);
            akeno_response_free(&user);
            return code;
        }
        /* GET /users/:handle returns a PublicProfile: { user: { id, ... }, ... } */
        const char *user_id = akeno_json_string(akeno_json_path(akeno_response_json(&user), "user.id"), "");
        if (!valid_id(user_id)) {
            fprintf(stderr, "The server returned an unexpected profile.\n");
            akeno_response_free(&user);
            return EXIT_API;
        }
        char body[128];
        snprintf(body, sizeof(body), "{\"userId\":\"%s\"}", user_id);
        akeno_response_free(&user);
        akeno_response conv;
        status = akeno_request(client, "POST", "/conversations", body, &conv);
        if (status != AKENO_OK) {
            int code = report_failure("Opening the conversation", status, &conv);
            akeno_response_free(&conv);
            return code;
        }
        snprintf(conversation, sizeof(conversation), "%s", akeno_json_get_string(akeno_response_json(&conv), "id", ""));
        akeno_response_free(&conv);
        if (!valid_id(conversation)) return EXIT_API;
    }
    char body[4600] = "{\"body\":";
    char uuid[37];
    random_uuid(uuid);
    if (append_json_string(body, sizeof(body), text) != 0) return EXIT_USAGE;
    size_t len = strlen(body);
    snprintf(body + len, sizeof(body) - len, ",\"clientMessageId\":\"%s\"}", uuid);
    char path[128];
    snprintf(path, sizeof(path), "/conversations/%s/messages", conversation);
    return call(client, "Sending the message", "POST", path, body, print_ok);
}

static void on_event(void *user, const char *type, const akeno_json *data) {
    int *remaining = user;
    printf("{\"type\":");
    print_string(type, stdout);
    printf(",\"data\":");
    print_json(data, stdout);
    printf("}\n");
    fflush(stdout);
    if (*remaining > 0 && --*remaining == 0) g_stop = 1;
}

static void on_state(void *user, const char *state, const char *detail) {
    (void)user;
    fprintf(stderr, "{\"level\":\"info\",\"event\":\"realtime.%s\",\"message\":\"", state);
    for (const char *p = detail; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', stderr);
        fputc(*p, stderr);
    }
    fputs("\"}\n", stderr);
}

static int cmd_caps(const char *credential_path, const char *firmware) {
    akeno_platform_facts facts = { 0 };
#if defined(AKENO_TARGET_PS5)
    facts.platform = AKENO_PLATFORM_PS5;
    facts.platform_name = "PlayStation 5";
#else
    facts.platform = AKENO_PLATFORM_HOST;
#if defined(__linux__)
    facts.platform_name = "Linux";
#else
    facts.platform_name = "Host";
#endif
#endif
    facts.firmware = firmware;
    facts.has_sockets = 1;
    facts.has_tls = 1;
    char dir[1100];
    snprintf(dir, sizeof(dir), "%s", credential_path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0'; else snprintf(dir, sizeof(dir), ".");
    facts.has_storage = access(dir[0] ? dir : "/", W_OK) == 0;
    akeno_capability_report report;
    akeno_caps_evaluate(&facts, &report);
    printf("Platform: %s\n", facts.platform_name);
    printf("Firmware: %s (%s)\n", firmware ? firmware : "not reported", report.firmware.note);
    for (int f = 0; f < AKENO_FEATURE_COUNT; f++) {
        printf("  %-22s %-12s %s\n", akeno_feature_name((akeno_feature)f), akeno_cap_state_name(report.features[f].state),
               report.features[f].reason);
    }
    return 0;
}

static void usage(void) {
    fprintf(stderr,
            "AKENO NETWORK device client " AKENO_CLIENT_VERSION "\n\n"
            "Usage: akeno-cli [options] <command> [arguments]\n\n"
            "Options:\n"
            "  --server URL         AKENO server (or AKENO_SERVER), e.g. https://akeno.example.org\n"
            "  --ca-file PEM        Trust these CA certificates instead of the system store\n"
            "  --credentials PATH   Device credential file (or AKENO_CREDENTIALS)\n"
            "  --name NAME          Device name shown on the website when pairing\n"
            "  --json               Print raw JSON responses\n"
            "  --duration SECONDS   listen: stop after this many seconds\n"
            "  --max-events N       listen: stop after N events\n"
            "  --firmware X.YY      caps: firmware version to evaluate\n"
            "  -v, --verbose        More diagnostics on stderr\n\n"
            "Commands:\n"
            "  pair                 Pair this device with your account (approve the code on the website)\n"
            "  status               Show the paired account and granted permissions\n"
            "  caps                 Show which features are available on this system\n"
            "  me | friends | requests | conversations | invitations | party | parties | sessions\n"
            "  messages <conversationId>\n"
            "  send <onlineId|conversationId> <text>\n"
            "  accept <invitationId> | decline <invitationId>\n"
            "  presence <online|away|dnd|invisible>\n"
            "  listen               Print live events as JSON lines until interrupted\n"
            "  logout               Unpair this device and delete its credential\n");
}

int main(int argc, char **argv) {
    const char *server = getenv("AKENO_SERVER");
    const char *credentials = getenv("AKENO_CREDENTIALS");
    const char *ca_file = NULL;
    const char *name = "AKENO device client";
    const char *firmware = NULL;
    int duration_s = 0;
    int max_events = 0;
    int argi = 1;
    for (; argi < argc && argv[argi][0] == '-'; argi++) {
        const char *opt = argv[argi];
        const char *value = argi + 1 < argc ? argv[argi + 1] : NULL;
        if (strcmp(opt, "--json") == 0) g_json = 1;
        else if (strcmp(opt, "-v") == 0 || strcmp(opt, "--verbose") == 0) g_level = AKENO_LOG_DEBUG;
        else if (strcmp(opt, "--help") == 0 || strcmp(opt, "-h") == 0) { usage(); return 0; }
        else if (!value) { usage(); return EXIT_USAGE; }
        else if (strcmp(opt, "--server") == 0) { server = value; argi++; }
        else if (strcmp(opt, "--ca-file") == 0) { ca_file = value; argi++; }
        else if (strcmp(opt, "--credentials") == 0) { credentials = value; argi++; }
        else if (strcmp(opt, "--name") == 0) { name = value; argi++; }
        else if (strcmp(opt, "--firmware") == 0) { firmware = value; argi++; }
        else if (strcmp(opt, "--duration") == 0) { duration_s = atoi(value); argi++; }
        else if (strcmp(opt, "--max-events") == 0) { max_events = atoi(value); argi++; }
        else { usage(); return EXIT_USAGE; }
    }
    if (argi >= argc) { usage(); return EXIT_USAGE; }
    const char *command = argv[argi++];

    char default_credentials[1024];
    if (!credentials) {
        const char *home = getenv("HOME");
        snprintf(default_credentials, sizeof(default_credentials), "%s/.config/akeno/device.json", home ? home : ".");
        credentials = default_credentials;
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/.config", home ? home : ".");
        mkdir(dir, 0700);
        snprintf(dir, sizeof(dir), "%s/.config/akeno", home ? home : ".");
        mkdir(dir, 0700);
    }

    if (strcmp(command, "caps") == 0) return cmd_caps(credentials, firmware);
    if (!server) {
        fprintf(stderr, "Set the server with --server https://... or the AKENO_SERVER environment variable.\n");
        return EXIT_USAGE;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    if (akeno_global_init() != AKENO_OK) {
        fprintf(stderr, "Could not initialize TLS.\n");
        return EXIT_NETWORK;
    }
    akeno_config config = { 0 };
    config.base_url = server;
    config.ca_file = ca_file;
    config.credential_path = credentials;
    config.log = log_line;
    config.log_level = g_level;
    const char *io_timeout = getenv("AKENO_IO_TIMEOUT_MS");
    if (io_timeout) config.io_timeout_ms = atoi(io_timeout);
    const char *connect_timeout = getenv("AKENO_CONNECT_TIMEOUT_MS");
    if (connect_timeout) config.connect_timeout_ms = atoi(connect_timeout);
    akeno_client *client = NULL;
    akeno_status status = akeno_client_create(&config, &client);
    if (status != AKENO_OK) {
        fprintf(stderr, "Invalid configuration: %s\n", akeno_status_name(status));
        akeno_global_cleanup();
        return exit_code_for(status);
    }

    int code = 0;
    const char *arg1 = argi < argc ? argv[argi] : NULL;
    const char *arg2 = argi + 1 < argc ? argv[argi + 1] : NULL;
    char path[160];
    if (strcmp(command, "pair") == 0) {
        akeno_pairing pairing;
        akeno_response response;
        status = akeno_pairing_start(client, name, "PlayStation 5 (homebrew)", &pairing, &response);
        if (status != AKENO_OK) {
            code = report_failure("Starting pairing", status, &response);
        } else {
            printf("To pair this device, open %s\nand enter the code: %s\n(or open %s)\n", pairing.verification_uri,
                   pairing.user_code, pairing.verification_uri_complete);
            printf("The code expires in %d minutes. Waiting for approval...\n", pairing.expires_in / 60);
            fflush(stdout);
            status = akeno_pairing_wait(client, &pairing, should_stop, NULL);
            if (status == AKENO_OK) printf("Paired as %s.\n", akeno_client_online_id(client));
            else if (status == AKENO_ERR_PAIRING_DENIED) fprintf(stderr, "Pairing was denied on the website.\n");
            else if (status == AKENO_ERR_PAIRING_EXPIRED) fprintf(stderr, "The code expired. Run pair again.\n");
            else fprintf(stderr, "Pairing failed: %s\n", akeno_status_name(status));
            code = exit_code_for(status);
        }
        akeno_response_free(&response);
    } else if (strcmp(command, "status") == 0) {
        code = call(client, "Reading device status", "GET", "/devices/current", NULL, NULL);
    } else if (strcmp(command, "me") == 0) {
        code = call(client, "Reading your profile", "GET", "/users/me", NULL, print_me);
    } else if (strcmp(command, "friends") == 0) {
        code = call(client, "Reading friends", "GET", "/friends", NULL, print_friends);
    } else if (strcmp(command, "requests") == 0) {
        code = call(client, "Reading friend requests", "GET", "/friends/requests", NULL, NULL);
    } else if (strcmp(command, "conversations") == 0) {
        code = call(client, "Reading conversations", "GET", "/conversations", NULL, print_conversations);
    } else if (strcmp(command, "messages") == 0 && valid_id(arg1)) {
        snprintf(path, sizeof(path), "/conversations/%s/messages?limit=50", arg1);
        code = call(client, "Reading messages", "GET", path, NULL, print_messages);
    } else if (strcmp(command, "send") == 0) {
        code = cmd_send(client, arg1, arg2);
    } else if (strcmp(command, "invitations") == 0) {
        code = call(client, "Reading invitations", "GET", "/invitations", NULL, print_invitations);
    } else if ((strcmp(command, "accept") == 0 || strcmp(command, "decline") == 0) && valid_id(arg1)) {
        snprintf(path, sizeof(path), "/invitations/%s/%s", arg1, command);
        code = call(client, "Answering the invitation", "POST", path, NULL, print_ok);
    } else if (strcmp(command, "party") == 0) {
        code = call(client, "Reading your party", "GET", "/parties/current", NULL, print_party);
    } else if (strcmp(command, "parties") == 0) {
        code = call(client, "Reading parties", "GET", "/parties", NULL, NULL);
    } else if (strcmp(command, "sessions") == 0) {
        code = call(client, "Reading game sessions", "GET", "/game-sessions", NULL, print_sessions);
    } else if (strcmp(command, "presence") == 0 && arg1 &&
               (strcmp(arg1, "online") == 0 || strcmp(arg1, "away") == 0 || strcmp(arg1, "dnd") == 0 || strcmp(arg1, "invisible") == 0)) {
        char body[48];
        snprintf(body, sizeof(body), "{\"status\":\"%s\"}", arg1);
        code = call(client, "Changing your status", "PUT", "/presence", body, print_ok);
    } else if (strcmp(command, "listen") == 0) {
        if (duration_s > 0) g_stop_at_ms = now_ms() + (long long)duration_s * 1000;
        int remaining = max_events;
        akeno_realtime_options options = { 0 };
        options.on_event = on_event;
        options.on_state = on_state;
        options.should_stop = should_stop;
        options.user = &remaining;
        const char *max_failures = getenv("AKENO_MAX_RECONNECTS");
        if (max_failures) options.max_consecutive_failures = atoi(max_failures);
        const char *backoff = getenv("AKENO_BACKOFF_BASE_MS");
        if (backoff) options.backoff_base_ms = atoi(backoff);
        const char *heartbeat = getenv("AKENO_HEARTBEAT_MS");
        if (heartbeat) options.heartbeat_interval_ms = atoi(heartbeat);
        status = akeno_realtime_run(client, &options);
        if (status == AKENO_ERR_UNAUTHORIZED) {
            fprintf(stderr, "This device was unpaired on the server. Run: akeno-cli pair\n");
            akeno_forget_credentials(client);
        }
        code = exit_code_for(status);
    } else if (strcmp(command, "logout") == 0) {
        status = akeno_logout(client);
        if (status == AKENO_OK) printf("This device was unpaired and its credential deleted.\n");
        code = exit_code_for(status);
    } else {
        usage();
        code = EXIT_USAGE;
    }
    akeno_client_destroy(client);
    akeno_global_cleanup();
    return code;
}
