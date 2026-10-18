/*
 * Semantic versions (semver.org 2.0.0) for the updater: parsing and precedence, including
 * pre-release identifiers ("1.2.0-beta.1" < "1.2.0-beta.2" < "1.2.0-rc.1" < "1.2.0"). Build
 * metadata ("+abc") is accepted and ignored for precedence, as the specification requires.
 */
#ifndef AKN_SEMVER_H
#define AKN_SEMVER_H

#include <stddef.h>

#define AKN_SEMVER_TEXT_MAX 64

typedef struct {
    unsigned long major, minor, patch;
    char pre[AKN_SEMVER_TEXT_MAX];   /* "" for a release; else the dot-separated identifiers */
    char build[AKN_SEMVER_TEXT_MAX]; /* ignored for precedence */
} akn_semver;

/*
 * Parses a version. A leading "v" is not accepted (tags carry their own prefix, see
 * update_release.h). Rejects leading zeros, empty identifiers, characters outside
 * [0-9A-Za-z-] and numbers that do not fit. Returns 0 on success, -1 otherwise.
 */
int akn_semver_parse(const char *text, akn_semver *out);

/* Precedence: <0, 0 or >0. */
int akn_semver_compare(const akn_semver *a, const akn_semver *b);

/* Parses both and compares; returns -2 when either does not parse. */
int akn_semver_compare_text(const char *a, const char *b);

int akn_semver_is_prerelease(const akn_semver *v);

/* "1.2.0-beta.1" (without build metadata). */
void akn_semver_format(const akn_semver *v, char *out, size_t out_len);

#endif
