#ifndef AKN_TEST_H
#define AKN_TEST_H

#include <stdio.h>
#include <string.h>

extern int g_failures;
extern int g_checks;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        g_checks++;                                                                 \
        if (!(cond)) {                                                              \
            g_failures++;                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                           \
    } while (0)

#define CHECK_STR(actual, expected)                                                                         \
    do {                                                                                                    \
        const char *a_ = (actual);                                                                          \
        const char *e_ = (expected);                                                                        \
        g_checks++;                                                                                         \
        if (!a_ || strcmp(a_, e_) != 0) {                                                                   \
            g_failures++;                                                                                   \
            fprintf(stderr, "%s:%d: expected \"%s\", got \"%s\"\n", __FILE__, __LINE__, e_, a_ ? a_ : "(null)"); \
        }                                                                                                   \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                                                     \
    do {                                                                                                     \
        const char *h_ = (haystack);                                                                         \
        const char *n_ = (needle);                                                                           \
        g_checks++;                                                                                          \
        if (!h_ || !strstr(h_, n_)) {                                                                        \
            g_failures++;                                                                                    \
            fprintf(stderr, "%s:%d: \"%s\" not found in \"%s\"\n", __FILE__, __LINE__, n_, h_ ? h_ : "(null)"); \
        }                                                                                                    \
    } while (0)

#endif
