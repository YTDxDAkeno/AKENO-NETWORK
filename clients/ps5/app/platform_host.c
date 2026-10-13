/*
 * Host (Linux) platform layer for running and testing the console app on a PC. Notifications and
 * browser requests are written to stdout as JSON lines so the test harness can observe them.
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

void akeno_platform_debug_line(const char *line) {
    fprintf(stderr, "%s\n", line);
}
