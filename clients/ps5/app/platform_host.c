/*
 * Host (Linux) platform layer for running and testing the console app on a PC. Notifications and
 * browser requests are written to stdout as JSON lines so the test harness can observe them.
 *
 * Game detection is simulated: AKENO_SIM_PROCESSES names a file with one process per line,
 * "<pid> <title-id or -> [stopped]", re-read on every scan (a missing file means no processes).
 * AKENO_SIM_METADATA_ROOT is the directory that stands in for the console's root when title
 * names are looked up. Without these variables the host build reports no game detection.
 */
#define _POSIX_C_SOURCE 200809L
#include "platform.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_out = PTHREAD_MUTEX_INITIALIZER;

static void print_json_string(const char *s) {
    fputc('"', stdout);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(stdout, "\\%c", *p);
        else if (*p < 0x20) fprintf(stdout, "\\u%04x", *p);
        else fputc(*p, stdout);
    }
    fputc('"', stdout);
}

void akeno_platform_init(akeno_platform_info *info) {
    memset(info, 0, sizeof(*info));
#if defined(__linux__)
    info->name = "Linux";
#else
    info->name = "Host";
#endif
    info->notifications = "printed to standard output (host build)";
    info->browser = "URL printed to standard output (host build)";
    const char *sim = getenv("AKENO_SIM_PROCESSES");
    info->game_detection = sim && sim[0] ? "simulated process list (AKENO_SIM_PROCESSES)" : "not available on this platform";
    info->actionable = "printed to standard output (host build)";
}

void akeno_platform_cleanup(void) {}

int akeno_platform_notify(const char *text) {
    pthread_mutex_lock(&g_out);
    fputs("{\"event\":\"notification\",\"text\":", stdout);
    print_json_string(text);
    fputs("}\n", stdout);
    fflush(stdout);
    pthread_mutex_unlock(&g_out);
    return 0;
}

int akeno_platform_open_browser(const char *url) {
    /* Test switch: simulate a console where the browser cannot be launched. */
    const char *fail = getenv("AKENO_TEST_BROWSER_FAILS");
    int failed = fail && fail[0] == '1';
    pthread_mutex_lock(&g_out);
    fputs("{\"event\":\"open-browser\",\"url\":", stdout);
    print_json_string(url);
    fputs(failed ? ",\"failed\":true}\n" : "}\n", stdout);
    fflush(stdout);
    pthread_mutex_unlock(&g_out);
    return failed ? -1 : 0;
}

int akeno_platform_notify_action(const char *title, const char *text, const char *button, const char *url) {
    pthread_mutex_lock(&g_out);
    fputs("{\"event\":\"notification\",\"text\":", stdout);
    print_json_string(title);
    fputs(",\"sub\":", stdout);
    print_json_string(text);
    fputs(",\"action\":{\"label\":", stdout);
    print_json_string(button);
    fputs(",\"url\":", stdout);
    print_json_string(url);
    fputs("}}\n", stdout);
    fflush(stdout);
    pthread_mutex_unlock(&g_out);
    return 0;
}

int akeno_platform_list_apps(akeno_proc_info *out, size_t max, size_t *total, char *err, size_t err_len) {
    *total = 0;
    const char *path = getenv("AKENO_SIM_PROCESSES");
    if (!path || !path[0]) {
        snprintf(err, err_len, "Game detection is only available on the PS5.");
        return -1;
    }
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    size_t n = 0;
    while (fgets(line, sizeof(line), f)) {
        int pid = 0;
        char title[32] = "", state[16] = "";
        if (line[0] == '#' || sscanf(line, "%d %31s %15s", &pid, title, state) < 2) continue;
        (*total)++;
        if (n >= max) continue;
        akeno_proc_info *o = &out[n++];
        memset(o, 0, sizeof(*o));
        o->pid = pid;
        o->state = strcmp(state, "stopped") == 0 ? AKENO_PROC_STOPPED : AKENO_PROC_ACTIVE;
        o->has_app_info = strcmp(title, "-") != 0;
        if (o->has_app_info) snprintf(o->title_id, sizeof(o->title_id), "%s", title);
        snprintf(o->comm, sizeof(o->comm), "sim%d", pid);
    }
    fclose(f);
    return (int)n;
}

const char *akeno_platform_metadata_root(void) {
    const char *root = getenv("AKENO_SIM_METADATA_ROOT");
    return root && root[0] ? root : NULL;
}

void akeno_platform_debug_line(const char *line) {
    fprintf(stderr, "%s\n", line);
}
