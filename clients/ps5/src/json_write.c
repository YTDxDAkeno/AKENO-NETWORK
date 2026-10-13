#include "json_write.h"

/* Recursion depth is bounded by the parser (AKENO_JSON_MAX_DEPTH). */
int akeno_json_write(const akeno_json *node, akeno_buf *out) {
    if (!node) return akeno_buf_append_str(out, "null");
    switch (node->type) {
    case AKENO_JSON_NULL: return akeno_buf_append_str(out, "null");
    case AKENO_JSON_BOOL: return akeno_buf_append_str(out, node->boolean ? "true" : "false");
    case AKENO_JSON_NUMBER: return akeno_buf_append_str(out, node->string); /* Original, validated text. */
    case AKENO_JSON_STRING: return akeno_buf_append_json_string(out, node->string);
    case AKENO_JSON_ARRAY:
    case AKENO_JSON_OBJECT: {
        int object = node->type == AKENO_JSON_OBJECT;
        if (akeno_buf_append(out, object ? "{" : "[", 1) != 0) return -1;
        for (const akeno_json *c = node->child; c; c = c->next) {
            if (object && (akeno_buf_append_json_string(out, c->key) != 0 || akeno_buf_append(out, ":", 1) != 0)) return -1;
            if (akeno_json_write(c, out) != 0) return -1;
            if (c->next && akeno_buf_append(out, ",", 1) != 0) return -1;
        }
        return akeno_buf_append(out, object ? "}" : "]", 1);
    }
    }
    return -1;
}
