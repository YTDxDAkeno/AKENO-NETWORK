/*
 * Strict RFC 8259 JSON parser producing a read-only tree. All nodes live in one arena that is
 * released with akeno_json_free(). Nesting depth and input size are bounded, so malformed or
 * hostile server responses fail cleanly instead of exhausting memory or the stack.
 */
#ifndef AKENO_JSON_H
#define AKENO_JSON_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    AKENO_JSON_NULL,
    AKENO_JSON_BOOL,
    AKENO_JSON_NUMBER,
    AKENO_JSON_STRING,
    AKENO_JSON_ARRAY,
    AKENO_JSON_OBJECT
} akeno_json_type;

typedef struct akeno_json akeno_json;
struct akeno_json {
    akeno_json_type type;
    const char *key;        /* Member name when inside an object. */
    const char *string;     /* STRING: decoded UTF-8; NUMBER: original text. */
    double number;
    int boolean;
    akeno_json *child;      /* First element/member. */
    akeno_json *next;       /* Next sibling. */
    size_t count;           /* Number of children. */
};

typedef struct akeno_json_doc akeno_json_doc;

#define AKENO_JSON_MAX_DEPTH 32

/* Parses len bytes. Returns NULL and fills err (if non-NULL) on failure. */
akeno_json_doc *akeno_json_parse(const char *text, size_t len, char *err, size_t err_len);
const akeno_json *akeno_json_root(const akeno_json_doc *doc);
void akeno_json_free(akeno_json_doc *doc);

/* Accessors return NULL / fallback values when the shape does not match. */
const akeno_json *akeno_json_get(const akeno_json *object, const char *key);
const akeno_json *akeno_json_at(const akeno_json *array, size_t index);
/* Follows a dotted path such as "error.code" through objects. */
const akeno_json *akeno_json_path(const akeno_json *node, const char *path);
const char *akeno_json_string(const akeno_json *node, const char *fallback);
const char *akeno_json_get_string(const akeno_json *object, const char *key, const char *fallback);
int akeno_json_get_bool(const akeno_json *object, const char *key, int fallback);
/* Integers must be exact and within int64 range. Returns 0 on success. */
int akeno_json_int64(const akeno_json *node, int64_t *out);

#endif
