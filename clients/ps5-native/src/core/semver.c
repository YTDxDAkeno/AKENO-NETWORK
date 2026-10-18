#include "semver.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int parse_number(const char **p, unsigned long *out) {
    const char *s = *p;
    if (!isdigit((unsigned char)*s)) return -1;
    if (s[0] == '0' && isdigit((unsigned char)s[1])) return -1; /* no leading zeros */
    unsigned long v = 0;
    while (isdigit((unsigned char)*s)) {
        unsigned d = (unsigned)(*s - '0');
        if (v > (ULONG_MAX - d) / 10) return -1;
        v = v * 10 + d;
        s++;
    }
    *out = v;
    *p = s;
    return 0;
}

static int all_digits(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return n > 0;
}

/* Dot-separated identifiers of [0-9A-Za-z-]; numeric ones without leading zeros when strict. */
static int valid_identifiers(const char *s, size_t n, int numeric_strict) {
    if (n == 0) return 0;
    size_t start = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || s[i] == '.') {
            size_t len = i - start;
            if (len == 0) return 0;
            if (numeric_strict && all_digits(s + start, len) && len > 1 && s[start] == '0') return 0;
            start = i + 1;
            continue;
        }
        if (!isalnum((unsigned char)s[i]) && s[i] != '-') return 0;
    }
    return 1;
}

int akn_semver_parse(const char *text, akn_semver *out) {
    if (!text || !out) return -1;
    memset(out, 0, sizeof(*out));
    const char *p = text;
    if (parse_number(&p, &out->major) || *p++ != '.') return -1;
    if (parse_number(&p, &out->minor) || *p++ != '.') return -1;
    if (parse_number(&p, &out->patch)) return -1;
    if (*p == '-') {
        p++;
        size_t n = strcspn(p, "+");
        if (n >= sizeof(out->pre) || !valid_identifiers(p, n, 1)) return -1;
        memcpy(out->pre, p, n);
        out->pre[n] = '\0';
        p += n;
    }
    if (*p == '+') {
        p++;
        size_t n = strlen(p);
        if (n >= sizeof(out->build) || !valid_identifiers(p, n, 0)) return -1;
        memcpy(out->build, p, n + 1);
        p += n;
    }
    return *p == '\0' ? 0 : -1;
}

static int cmp_ul(unsigned long a, unsigned long b) { return a < b ? -1 : a > b ? 1 : 0; }

/* semver.org 11.4: identifier by identifier; numeric < alphanumeric; a shorter list is lower. */
static int compare_pre(const char *a, const char *b) {
    while (*a && *b) {
        size_t la = strcspn(a, "."), lb = strcspn(b, ".");
        int na = all_digits(a, la), nb = all_digits(b, lb);
        int c;
        if (na && nb) {
            c = la != lb ? (la < lb ? -1 : 1) : strncmp(a, b, la); /* no leading zeros: length orders */
        } else if (na != nb) {
            c = na ? -1 : 1;
        } else {
            size_t m = la < lb ? la : lb;
            c = strncmp(a, b, m);
            if (c == 0 && la != lb) c = la < lb ? -1 : 1;
        }
        if (c) return c < 0 ? -1 : 1;
        a += la;
        b += lb;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

int akn_semver_compare(const akn_semver *a, const akn_semver *b) {
    int c = cmp_ul(a->major, b->major);
    if (!c) c = cmp_ul(a->minor, b->minor);
    if (!c) c = cmp_ul(a->patch, b->patch);
    if (c) return c;
    int pa = a->pre[0] != '\0', pb = b->pre[0] != '\0';
    if (pa != pb) return pa ? -1 : 1; /* a pre-release is lower than its release */
    return pa ? compare_pre(a->pre, b->pre) : 0;
}

int akn_semver_compare_text(const char *a, const char *b) {
    akn_semver va, vb;
    if (akn_semver_parse(a, &va) || akn_semver_parse(b, &vb)) return -2;
    return akn_semver_compare(&va, &vb);
}

int akn_semver_is_prerelease(const akn_semver *v) { return v->pre[0] != '\0'; }

void akn_semver_format(const akn_semver *v, char *out, size_t out_len) {
    if (!out_len) return;
    if (v->pre[0]) snprintf(out, out_len, "%lu.%lu.%lu-%s", v->major, v->minor, v->patch, v->pre);
    else snprintf(out, out_len, "%lu.%lu.%lu", v->major, v->minor, v->patch);
}
