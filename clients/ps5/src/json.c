#include "akeno/json.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct arena_block {
    struct arena_block *next;
    size_t used;
    size_t size;
    max_align_t data[];
} arena_block;

struct akeno_json_doc {
    arena_block *blocks;
    akeno_json *root;
};

typedef struct {
    const char *p;
    const char *end;
    akeno_json_doc *doc;
    char *err;
    size_t err_len;
} parser;

static void *arena_alloc(akeno_json_doc *doc, size_t size) {
    size = (size + sizeof(max_align_t) - 1) / sizeof(max_align_t) * sizeof(max_align_t);
    arena_block *block = doc->blocks;
    if (!block || block->size - block->used < size) {
        size_t want = size > 16384 ? size : 16384;
        block = malloc(sizeof(arena_block) + want);
        if (!block) return NULL;
        block->next = doc->blocks;
        block->used = 0;
        block->size = want;
        doc->blocks = block;
    }
    void *out = (char *)block->data + block->used;
    block->used += size;
    return out;
}

static void fail(parser *ps, const char *message) {
    if (ps->err && ps->err_len && ps->err[0] == '\0') snprintf(ps->err, ps->err_len, "%s", message);
}

static void skip_ws(parser *ps) {
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++;
}

static akeno_json *new_node(parser *ps, akeno_json_type type) {
    akeno_json *node = arena_alloc(ps->doc, sizeof(akeno_json));
    if (!node) {
        fail(ps, "out of memory");
        return NULL;
    }
    memset(node, 0, sizeof(*node));
    node->type = type;
    return node;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int read_hex4(parser *ps, unsigned *out) {
    if (ps->end - ps->p < 4) return -1;
    unsigned value = 0;
    for (int i = 0; i < 4; i++) {
        int h = hex_value(ps->p[i]);
        if (h < 0) return -1;
        value = (value << 4) | (unsigned)h;
    }
    ps->p += 4;
    *out = value;
    return 0;
}

static size_t encode_utf8(unsigned cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Parses a string literal starting at the opening quote. Decoded output is never longer than the input. */
static const char *parse_string_raw(parser *ps) {
    if (ps->p >= ps->end || *ps->p != '"') { fail(ps, "expected string"); return NULL; }
    ps->p++;
    const char *scan = ps->p;
    while (scan < ps->end && *scan != '"') scan += (*scan == '\\' && scan + 1 < ps->end) ? 2 : 1;
    if (scan >= ps->end) { fail(ps, "unterminated string"); return NULL; }
    char *out = arena_alloc(ps->doc, (size_t)(scan - ps->p) + 1);
    if (!out) { fail(ps, "out of memory"); return NULL; }
    size_t n = 0;
    while (ps->p < ps->end && *ps->p != '"') {
        unsigned char c = (unsigned char)*ps->p;
        if (c < 0x20) { fail(ps, "control character in string"); return NULL; }
        if (c != '\\') { out[n++] = (char)c; ps->p++; continue; }
        ps->p++;
        if (ps->p >= ps->end) { fail(ps, "bad escape"); return NULL; }
        char e = *ps->p++;
        switch (e) {
        case '"': out[n++] = '"'; break;
        case '\\': out[n++] = '\\'; break;
        case '/': out[n++] = '/'; break;
        case 'b': out[n++] = '\b'; break;
        case 'f': out[n++] = '\f'; break;
        case 'n': out[n++] = '\n'; break;
        case 'r': out[n++] = '\r'; break;
        case 't': out[n++] = '\t'; break;
        case 'u': {
            unsigned cp;
            if (read_hex4(ps, &cp) != 0) { fail(ps, "bad unicode escape"); return NULL; }
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                unsigned low;
                if (ps->end - ps->p < 6 || ps->p[0] != '\\' || ps->p[1] != 'u') { fail(ps, "unpaired surrogate"); return NULL; }
                ps->p += 2;
                if (read_hex4(ps, &low) != 0 || low < 0xDC00 || low > 0xDFFF) { fail(ps, "unpaired surrogate"); return NULL; }
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                fail(ps, "unpaired surrogate");
                return NULL;
            }
            if (cp == 0) { fail(ps, "NUL in string"); return NULL; }
            n += encode_utf8(cp, out + n);
            break;
        }
        default: fail(ps, "bad escape"); return NULL;
        }
    }
    ps->p++; /* closing quote */
    out[n] = '\0';
    return out;
}

static akeno_json *parse_value(parser *ps, int depth);

static akeno_json *parse_number(parser *ps) {
    const char *start = ps->p;
    if (ps->p < ps->end && *ps->p == '-') ps->p++;
    if (ps->p >= ps->end) { fail(ps, "bad number"); return NULL; }
    if (*ps->p == '0') {
        ps->p++;
    } else if (*ps->p >= '1' && *ps->p <= '9') {
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    } else {
        fail(ps, "bad number");
        return NULL;
    }
    if (ps->p < ps->end && *ps->p == '.') {
        ps->p++;
        if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') { fail(ps, "bad number"); return NULL; }
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    }
    if (ps->p < ps->end && (*ps->p == 'e' || *ps->p == 'E')) {
        ps->p++;
        if (ps->p < ps->end && (*ps->p == '+' || *ps->p == '-')) ps->p++;
        if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9') { fail(ps, "bad number"); return NULL; }
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') ps->p++;
    }
    size_t len = (size_t)(ps->p - start);
    if (len > 64) { fail(ps, "number too long"); return NULL; }
    akeno_json *node = new_node(ps, AKENO_JSON_NUMBER);
    if (!node) return NULL;
    char *text = arena_alloc(ps->doc, len + 1);
    if (!text) { fail(ps, "out of memory"); return NULL; }
    memcpy(text, start, len);
    text[len] = '\0';
    node->string = text;
    node->number = strtod(text, NULL);
    if (!isfinite(node->number)) { fail(ps, "number out of range"); return NULL; }
    return node;
}

static int literal(parser *ps, const char *word) {
    size_t n = strlen(word);
    if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, word, n) != 0) return 0;
    ps->p += n;
    return 1;
}

static akeno_json *parse_container(parser *ps, int depth, int object) {
    akeno_json *node = new_node(ps, object ? AKENO_JSON_OBJECT : AKENO_JSON_ARRAY);
    if (!node) return NULL;
    ps->p++; /* [ or { */
    skip_ws(ps);
    char close = object ? '}' : ']';
    if (ps->p < ps->end && *ps->p == close) { ps->p++; return node; }
    akeno_json *last = NULL;
    for (;;) {
        const char *key = NULL;
        skip_ws(ps);
        if (object) {
            key = parse_string_raw(ps);
            if (!key) return NULL;
            skip_ws(ps);
            if (ps->p >= ps->end || *ps->p != ':') { fail(ps, "expected ':'"); return NULL; }
            ps->p++;
        }
        akeno_json *child = parse_value(ps, depth + 1);
        if (!child) return NULL;
        child->key = key;
        if (last) last->next = child; else node->child = child;
        last = child;
        node->count++;
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') { ps->p++; continue; }
        if (ps->p < ps->end && *ps->p == close) { ps->p++; return node; }
        fail(ps, object ? "expected ',' or '}'" : "expected ',' or ']'");
        return NULL;
    }
}

static akeno_json *parse_value(parser *ps, int depth) {
    if (depth > AKENO_JSON_MAX_DEPTH) { fail(ps, "nesting too deep"); return NULL; }
    skip_ws(ps);
    if (ps->p >= ps->end) { fail(ps, "unexpected end of input"); return NULL; }
    switch (*ps->p) {
    case '{': return parse_container(ps, depth, 1);
    case '[': return parse_container(ps, depth, 0);
    case '"': {
        akeno_json *node = new_node(ps, AKENO_JSON_STRING);
        if (!node) return NULL;
        node->string = parse_string_raw(ps);
        return node->string ? node : NULL;
    }
    case 't': case 'f': case 'n': {
        akeno_json *node;
        if (literal(ps, "true")) { node = new_node(ps, AKENO_JSON_BOOL); if (node) node->boolean = 1; return node; }
        if (literal(ps, "false")) { node = new_node(ps, AKENO_JSON_BOOL); return node; }
        if (literal(ps, "null")) return new_node(ps, AKENO_JSON_NULL);
        fail(ps, "invalid literal");
        return NULL;
    }
    default:
        if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) return parse_number(ps);
        fail(ps, "unexpected character");
        return NULL;
    }
}

akeno_json_doc *akeno_json_parse(const char *text, size_t len, char *err, size_t err_len) {
    if (err && err_len) err[0] = '\0';
    akeno_json_doc *doc = calloc(1, sizeof(*doc));
    if (!doc) return NULL;
    parser ps = { text, text + len, doc, err, err_len };
    doc->root = text ? parse_value(&ps, 0) : NULL;
    if (doc->root) {
        skip_ws(&ps);
        if (ps.p != ps.end) {
            fail(&ps, "trailing data after JSON value");
            doc->root = NULL;
        }
    } else if (!text) {
        fail(&ps, "no input");
    }
    if (!doc->root) {
        akeno_json_free(doc);
        return NULL;
    }
    return doc;
}

const akeno_json *akeno_json_root(const akeno_json_doc *doc) { return doc ? doc->root : NULL; }

void akeno_json_free(akeno_json_doc *doc) {
    if (!doc) return;
    arena_block *block = doc->blocks;
    while (block) {
        arena_block *next = block->next;
        memset(block->data, 0, block->size); /* Responses may contain tokens. */
        free(block);
        block = next;
    }
    free(doc);
}

const akeno_json *akeno_json_get(const akeno_json *object, const char *key) {
    if (!object || object->type != AKENO_JSON_OBJECT) return NULL;
    /* RFC 8259 leaves duplicate names undefined; the last one wins, as in JavaScript. */
    const akeno_json *found = NULL;
    for (const akeno_json *c = object->child; c; c = c->next)
        if (strcmp(c->key, key) == 0) found = c;
    return found;
}

const akeno_json *akeno_json_at(const akeno_json *array, size_t index) {
    if (!array || array->type != AKENO_JSON_ARRAY) return NULL;
    const akeno_json *c = array->child;
    while (c && index--) c = c->next;
    return c;
}

const akeno_json *akeno_json_path(const akeno_json *node, const char *path) {
    char segment[64];
    while (node && *path) {
        size_t n = strcspn(path, ".");
        if (n == 0 || n >= sizeof(segment)) return NULL;
        memcpy(segment, path, n);
        segment[n] = '\0';
        node = akeno_json_get(node, segment);
        path += n;
        if (*path == '.') path++;
    }
    return node;
}

const char *akeno_json_string(const akeno_json *node, const char *fallback) {
    return node && node->type == AKENO_JSON_STRING ? node->string : fallback;
}

const char *akeno_json_get_string(const akeno_json *object, const char *key, const char *fallback) {
    return akeno_json_string(akeno_json_get(object, key), fallback);
}

int akeno_json_get_bool(const akeno_json *object, const char *key, int fallback) {
    const akeno_json *node = akeno_json_get(object, key);
    return node && node->type == AKENO_JSON_BOOL ? node->boolean : fallback;
}

int akeno_json_int64(const akeno_json *node, int64_t *out) {
    if (!node || node->type != AKENO_JSON_NUMBER || strpbrk(node->string, ".eE")) return -1;
    errno = 0;
    char *end = NULL;
    long long value = strtoll(node->string, &end, 10);
    if (errno != 0 || !end || *end != '\0') return -1;
    *out = (int64_t)value;
    return 0;
}
