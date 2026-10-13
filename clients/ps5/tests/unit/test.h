#ifndef AKENO_TEST_H
#define AKENO_TEST_H

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

typedef void (*test_fn)(void);
void register_test(const char *name, test_fn fn);

#endif
