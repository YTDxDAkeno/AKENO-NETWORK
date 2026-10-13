/* Serializes a parsed JSON tree back to compact RFC 8259 text (used to forward API responses). */
#ifndef AKENO_JSON_WRITE_H
#define AKENO_JSON_WRITE_H

#include "akeno/json.h"
#include "buf.h"

/* Appends node (NULL is written as null). Returns 0, or -1 when out's limit is exceeded. */
int akeno_json_write(const akeno_json *node, akeno_buf *out);

#endif
