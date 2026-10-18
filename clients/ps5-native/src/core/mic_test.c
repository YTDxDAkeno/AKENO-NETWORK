#include "mic_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC "AKENO Native microphone test v1"

static const char *open_name(akn_mic_test_open o) {
    switch (o) {
    case AKN_MIC_TEST_OPENED: return "opened";
    case AKN_MIC_TEST_UNAVAILABLE: return "unavailable";
    case AKN_MIC_TEST_OPEN_FAILED: return "failed";
    }
    return "failed";
}

int akn_mic_test_succeeded(const akn_mic_test_record *r) {
    return r->open == AKN_MIC_TEST_OPENED && r->frames > 0 && r->closed;
}

/* Text fields are single-line and printable; anything else is replaced so a line stays one line. */
static void clean(char *dst, size_t len, const char *src) {
    size_t o = 0;
    for (; src && *src && o + 1 < len; src++) dst[o++] = (*src >= 0x20 && *src < 0x7f) ? *src : '?';
    dst[o] = '\0';
}

int akn_mic_test_format(const akn_mic_test_record *r, char *out, size_t out_len) {
    char build[sizeof(r->build)], backend[sizeof(r->backend)];
    clean(build, sizeof(build), r->build);
    clean(backend, sizeof(backend), r->backend);
    int n = snprintf(out, out_len,
                     MAGIC "\n"
                     "build: %s\n"
                     "backend: %s\n"
                     "open: %s\n"
                     "open_rc: 0x%08X\n"
                     "rate: %u\n"
                     "grain: %u\n"
                     "frames: %lu\n"
                     "peak_percent: %d\n"
                     "level_max: %d\n"
                     "level_avg: %d\n"
                     "read_errors: %lu\n"
                     "last_read_rc: 0x%08X\n"
                     "closed: %d\n"
                     "duration_ms: %lu\n",
                     build, backend, open_name(r->open), (unsigned)r->open_rc, r->rate, r->grain, r->frames, r->peak_percent,
                     r->level_max, r->level_avg, r->read_errors, (unsigned)r->last_read_rc, r->closed ? 1 : 0, r->duration_ms);
    return n < 0 || (size_t)n >= out_len ? -1 : n;
}

static int number(const char *v, unsigned long max, unsigned long *out) {
    char *end = NULL;
    if (*v < '0' || *v > '9') return -1;
    unsigned long x = strtoul(v, &end, 0);
    if (*end != '\0' || x > max) return -1;
    *out = x;
    return 0;
}

int akn_mic_test_parse(const char *text, akn_mic_test_record *r) {
    memset(r, 0, sizeof(*r));
    if (!text || strncmp(text, MAGIC "\n", sizeof(MAGIC)) != 0) return -1;
    const char *p = text + sizeof(MAGIC);
    unsigned seen = 0;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[256];
        if (len >= sizeof(line)) return -1;
        memcpy(line, p, len);
        line[len] = '\0';
        p += len + (nl ? 1 : 0);
        if (!len) continue;
        char *sep = strstr(line, ": ");
        if (!sep) return -1;
        *sep = '\0';
        const char *key = line, *v = sep + 2;
        unsigned long x = 0;
        if (!strcmp(key, "build")) {
            clean(r->build, sizeof(r->build), v);
            seen |= 1u << 0;
        } else if (!strcmp(key, "backend")) {
            clean(r->backend, sizeof(r->backend), v);
            seen |= 1u << 1;
        } else if (!strcmp(key, "open")) {
            if (!strcmp(v, "opened")) r->open = AKN_MIC_TEST_OPENED;
            else if (!strcmp(v, "unavailable")) r->open = AKN_MIC_TEST_UNAVAILABLE;
            else if (!strcmp(v, "failed")) r->open = AKN_MIC_TEST_OPEN_FAILED;
            else return -1;
            seen |= 1u << 2;
        } else if (!strcmp(key, "open_rc") || !strcmp(key, "last_read_rc")) {
            if (number(v, 0xFFFFFFFFUL, &x) != 0) return -1;
            if (!strcmp(key, "open_rc")) {
                r->open_rc = (int32_t)(uint32_t)x;
                seen |= 1u << 3;
            } else {
                r->last_read_rc = (int32_t)(uint32_t)x;
                seen |= 1u << 4;
            }
        } else if (!strcmp(key, "rate")) {
            if (number(v, 192000, &x) != 0) return -1;
            r->rate = (unsigned)x;
            seen |= 1u << 5;
        } else if (!strcmp(key, "grain")) {
            if (number(v, 65536, &x) != 0) return -1;
            r->grain = (unsigned)x;
            seen |= 1u << 6;
        } else if (!strcmp(key, "frames")) {
            if (number(v, 0xFFFFFFFFUL, &x) != 0) return -1;
            r->frames = x;
            seen |= 1u << 7;
        } else if (!strcmp(key, "peak_percent") || !strcmp(key, "level_max") || !strcmp(key, "level_avg")) {
            if (number(v, 100, &x) != 0) return -1;
            if (!strcmp(key, "peak_percent")) {
                r->peak_percent = (int)x;
                seen |= 1u << 8;
            } else if (!strcmp(key, "level_max")) {
                r->level_max = (int)x;
                seen |= 1u << 9;
            } else {
                r->level_avg = (int)x;
                seen |= 1u << 10;
            }
        } else if (!strcmp(key, "read_errors")) {
            if (number(v, 0xFFFFFFFFUL, &x) != 0) return -1;
            r->read_errors = x;
            seen |= 1u << 11;
        } else if (!strcmp(key, "closed")) {
            if (number(v, 1, &x) != 0) return -1;
            r->closed = (int)x;
            seen |= 1u << 12;
        } else if (!strcmp(key, "duration_ms")) {
            if (number(v, 0xFFFFFFFFUL, &x) != 0) return -1;
            r->duration_ms = x;
            seen |= 1u << 13;
        } else {
            return -1;
        }
    }
    return seen == (1u << 14) - 1 ? 0 : -1;
}

int akn_mic_test_describe(const akn_mic_test_record *r, char *out, size_t out_len) {
    char open_line[200];
    if (r->open == AKN_MIC_TEST_OPENED)
        snprintf(open_line, sizeof(open_line), "opened (%s, %u Hz, %u frames per read)", r->backend, r->rate, r->grain);
    else if (r->open == AKN_MIC_TEST_UNAVAILABLE)
        snprintf(open_line, sizeof(open_line), "no microphone function: %s", r->backend);
    else snprintf(open_line, sizeof(open_line), "could not be opened: 0x%08X (%s)", (unsigned)r->open_rc, r->backend);
    char errors[64] = "none";
    if (r->read_errors)
        snprintf(errors, sizeof(errors), "%lu (last 0x%08X)", r->read_errors, (unsigned)r->last_read_rc);
    int n = snprintf(out, out_len,
                     "Result:           %s\n"
                     "Build:            %s\n"
                     "Microphone:       %s\n"
                     "Captured:         %lu frames of 20 ms in %.1f s\n"
                     "Levels:           peak %d%%, loudest %d%%, average %d%%\n"
                     "Read errors:      %s\n"
                     "Closed:           %s\n"
                     "Local only:       yes (nothing was sent or stored; no audio is kept)\n",
                     akn_mic_test_succeeded(r) ? "the microphone captured sound" : "no sound was captured", r->build, open_line,
                     r->frames, r->duration_ms / 1000.0, r->peak_percent, r->level_max, r->level_avg, errors,
                     r->open != AKN_MIC_TEST_OPENED ? "not opened" : r->closed ? "yes" : "no");
    return n < 0 || (size_t)n >= out_len ? -1 : n;
}
