/* Game detection logic and metadata parsing; see game_detect.h. */
#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "game_detect.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "akeno/json.h"

#define PARAM_JSON_MAX_BYTES (256 * 1024)
#define PARAM_SFO_MAX_BYTES (64 * 1024)

int akeno_is_game_title_id(const char *id) {
    if (!id || strlen(id) != 9) return 0;
    if (strncmp(id, "PPSA", 4) != 0 && strncmp(id, "CUSA", 4) != 0) return 0;
    for (int i = 4; i < 9; i++)
        if (id[i] < '0' || id[i] > '9') return 0;
    return 1;
}

int akeno_looks_like_title_id(const char *s) {
    if (!s || strlen(s) != 9) return 0;
    for (int i = 0; i < 4; i++)
        if (s[i] < 'A' || s[i] > 'Z') return 0;
    for (int i = 4; i < 9; i++)
        if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

const char *akeno_scan_kind_name(akeno_scan_kind kind) {
    switch (kind) {
    case AKENO_SCAN_NONE: return "none";
    case AKENO_SCAN_GAME: return "game";
    case AKENO_SCAN_AMBIGUOUS: return "ambiguous";
    case AKENO_SCAN_FAILED: return "failed";
    case AKENO_SCAN_UNVERIFIED: return "unverified-layout";
    }
    return "none";
}

const char *akeno_layout_name(akeno_appinfo_layout layout) {
    switch (layout) {
    case AKENO_LAYOUT_UNKNOWN: return "unknown";
    case AKENO_LAYOUT_PS: return "samples/ps (title ID at offset 20)";
    case AKENO_LAYOUT_ALT: return "samples/test_privileges (title ID at offset 16)";
    case AKENO_LAYOUT_CONFLICT: return "conflicting";
    }
    return "unknown";
}

/* Copies a title ID found at raw[offset]: nine visible characters followed by a NUL byte. */
static void title_at(const unsigned char *raw, size_t len, size_t offset, char *out, size_t out_len) {
    out[0] = '\0';
    if (offset + 10 > len || out_len < 10) return;
    if (raw[offset + 9] != 0) return;
    char candidate[10];
    memcpy(candidate, raw + offset, 9);
    candidate[9] = '\0';
    if (akeno_looks_like_title_id(candidate)) memcpy(out, candidate, 10);
}

void akeno_appinfo_parse(const unsigned char *raw, size_t len, akeno_proc_info *out) {
    out->app_id = 0;
    if (len >= 4) out->app_id = (unsigned int)raw[0] | (unsigned int)raw[1] << 8 | (unsigned int)raw[2] << 16 | (unsigned int)raw[3] << 24;
    title_at(raw, len, AKENO_APPINFO_OFFSET_TITLE_PS, out->title_id, sizeof(out->title_id));
    title_at(raw, len, AKENO_APPINFO_OFFSET_TITLE_ALT, out->title_id_alt, sizeof(out->title_id_alt));
    memset(out->raw_head, 0, sizeof(out->raw_head));
    memcpy(out->raw_head, raw, len < sizeof(out->raw_head) ? len : sizeof(out->raw_head));
}

static void add_probe(akeno_game_scan *scan, const akeno_proc_info *p) {
    if (scan->probe_count >= AKENO_SCAN_PROBES) return;
    static const char hex[] = "0123456789abcdef";
    char *out = scan->probes[scan->probe_count++];
    size_t cap = sizeof(scan->probes[0]);
    int n = snprintf(out, cap, "%d %.12s:", p->pid, p->comm);
    size_t o = n > 0 && (size_t)n < cap ? (size_t)n : 0;
    for (size_t i = 0; i < AKENO_APPINFO_RAW_HEAD && o + 2 < cap; i++) {
        out[o++] = hex[p->raw_head[i] >> 4];
        out[o++] = hex[p->raw_head[i] & 15];
    }
    out[o] = '\0';
}

void akeno_game_classify(const akeno_proc_info *procs, size_t count, int self_pid, akeno_game_scan *scan) {
    memset(scan, 0, sizeof(*scan));
    scan->kind = AKENO_SCAN_NONE;
    scan->processes = (int)count;

    /* 1. Which offset holds the title ID on this console? Decided from this listing only. */
    for (size_t i = 0; i < count; i++) {
        const akeno_proc_info *p = &procs[i];
        if (p->pid == self_pid || !p->has_app_info) continue;
        int ps = akeno_looks_like_title_id(p->title_id);
        int alt = akeno_looks_like_title_id(p->title_id_alt);
        scan->layout_votes_ps += ps;
        scan->layout_votes_alt += alt;
        if (ps || alt) {
            scan->app_processes++;
            add_probe(scan, p);
        }
    }
    if (scan->layout_votes_ps && scan->layout_votes_alt) scan->layout = AKENO_LAYOUT_CONFLICT;
    else if (scan->layout_votes_ps) scan->layout = AKENO_LAYOUT_PS;
    else if (scan->layout_votes_alt) scan->layout = AKENO_LAYOUT_ALT;
    else scan->layout = AKENO_LAYOUT_UNKNOWN;
    if (scan->layout == AKENO_LAYOUT_CONFLICT) {
        scan->kind = AKENO_SCAN_UNVERIFIED;
        snprintf(scan->error, sizeof(scan->error), "Title IDs at both known offsets (%d/%d); nothing is reported.", scan->layout_votes_ps,
                 scan->layout_votes_alt);
        return;
    }

    /* 2. Games with running processes, read at the offset the console itself confirmed. */
    for (size_t i = 0; i < count; i++) {
        const akeno_proc_info *p = &procs[i];
        if (p->pid == self_pid || !p->has_app_info) continue;
        const char *title = scan->layout == AKENO_LAYOUT_ALT ? p->title_id_alt : p->title_id;
        if (!akeno_is_game_title_id(title)) continue;
        if (p->state != AKENO_PROC_ACTIVE) {
            scan->stopped_games++;
            continue;
        }
        int known = 0;
        for (int c = 0; c < scan->candidates && c < AKENO_SCAN_MAX_CANDIDATES; c++)
            if (strcmp(scan->candidate_ids[c], title) == 0) known = 1;
        if (known) continue;
        if (scan->candidates < AKENO_SCAN_MAX_CANDIDATES)
            snprintf(scan->candidate_ids[scan->candidates], AKENO_TITLE_ID_MAX, "%s", title);
        scan->candidates++;
    }
    if (scan->candidates == 1) {
        scan->kind = AKENO_SCAN_GAME;
        snprintf(scan->title_id, sizeof(scan->title_id), "%s", scan->candidate_ids[0]);
    } else if (scan->candidates > 1) {
        scan->kind = AKENO_SCAN_AMBIGUOUS;
    }
}

int akeno_game_debounce_update(akeno_game_debounce *d, const akeno_game_scan *scan, int confirm) {
    if (confirm < 1) confirm = 1;
    const char *seen = scan->kind == AKENO_SCAN_GAME ? scan->title_id : "";
    if (strcmp(seen, d->current) == 0) {
        d->pending[0] = '\0';
        d->pending_seen = 0;
        return 0;
    }
    if (d->pending_seen > 0 && strcmp(seen, d->pending) == 0) {
        d->pending_seen++;
    } else {
        snprintf(d->pending, sizeof(d->pending), "%s", seen);
        d->pending_seen = 1;
    }
    if (d->pending_seen < confirm) return 0;
    snprintf(d->current, sizeof(d->current), "%s", d->pending);
    d->pending[0] = '\0';
    d->pending_seen = 0;
    return 1;
}

/* ---- Names -------------------------------------------------------------------------------- */

/* Length of the UTF-8 sequence starting at s (strict: no overlongs, surrogates or > U+10FFFF), 0 if invalid. */
static size_t utf8_sequence(const unsigned char *s) {
    if (s[0] < 0x80) return 1;
    if (s[0] >= 0xC2 && s[0] <= 0xDF) return (s[1] & 0xC0) == 0x80 ? 2 : 0;
    if (s[0] >= 0xE0 && s[0] <= 0xEF) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        if (s[0] == 0xE0 && s[1] < 0xA0) return 0;
        if (s[0] == 0xED && s[1] >= 0xA0) return 0;
        return 3;
    }
    if (s[0] >= 0xF0 && s[0] <= 0xF4) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
        if (s[0] == 0xF0 && s[1] < 0x90) return 0;
        if (s[0] == 0xF4 && s[1] >= 0x90) return 0;
        return 4;
    }
    return 0;
}

int akeno_clean_title_name(char *name, size_t max_bytes) {
    unsigned char *s = (unsigned char *)name;
    size_t len = strlen(name);
    for (size_t i = 0; i < len;) {
        size_t n = utf8_sequence(s + i);
        if (n == 0) {
            name[0] = '\0';
            return -1;
        }
        if (n == 1 && (s[i] < 0x20 || s[i] == 0x7F)) s[i] = ' ';
        i += n;
    }
    /* Trim, then cut on a character boundary. */
    size_t start = 0;
    while (s[start] == ' ') start++;
    memmove(name, name + start, len - start + 1);
    len = strlen(name);
    if (len > max_bytes) {
        size_t cut = max_bytes;
        while (cut > 0 && (s[cut] & 0xC0) == 0x80) cut--;
        name[cut] = '\0';
        len = cut;
    }
    while (len > 0 && name[len - 1] == ' ') name[--len] = '\0';
    return len > 0 ? 0 : -1;
}

static int take_name(const char *value, char *out, size_t out_len) {
    if (!value || !value[0] || out_len < 2) return -1;
    snprintf(out, out_len, "%s", value);
    return akeno_clean_title_name(out, out_len - 1 < 128 ? out_len - 1 : 128);
}

int akeno_param_json_title(const char *data, size_t len, const char *expected_title_id, char *out, size_t out_len) {
    out[0] = '\0';
    akeno_json_doc *doc = akeno_json_parse(data, len, NULL, 0);
    if (!doc) return -1;
    const akeno_json *root = akeno_json_root(doc);
    int rc = -1;
    const char *file_title = akeno_json_get_string(root, "titleId", "");
    if (root && root->type == AKENO_JSON_OBJECT && !(file_title[0] && expected_title_id && strcmp(file_title, expected_title_id) != 0)) {
        const akeno_json *localized = akeno_json_get(root, "localizedParameters");
        const char *lang = akeno_json_get_string(localized, "defaultLanguage", "");
        if (lang[0]) rc = take_name(akeno_json_get_string(akeno_json_get(localized, lang), "titleName", NULL), out, out_len);
        if (rc != 0) rc = take_name(akeno_json_get_string(akeno_json_get(localized, "en-US"), "titleName", NULL), out, out_len);
        if (rc != 0 && localized && localized->type == AKENO_JSON_OBJECT) {
            for (const akeno_json *m = localized->child; m && rc != 0; m = m->next)
                if (m->type == AKENO_JSON_OBJECT) rc = take_name(akeno_json_get_string(m, "titleName", NULL), out, out_len);
        }
        if (rc != 0) rc = take_name(akeno_json_get_string(root, "titleName", NULL), out, out_len);
    }
    akeno_json_free(doc);
    if (rc != 0) out[0] = '\0';
    return rc;
}

static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* Copies the UTF-8 string value of an SFO entry (bounded by its declared length). */
static int sfo_string(const unsigned char *data, size_t len, uint32_t data_start, const unsigned char *entry, char *out, size_t out_len) {
    uint16_t fmt = le16(entry + 2);
    uint32_t data_len = le32(entry + 4);
    uint32_t data_offset = le32(entry + 12);
    if (fmt != 0x0204 && fmt != 0x0004) return -1;
    if ((uint64_t)data_start + data_offset + data_len > len || data_len == 0) return -1;
    size_t n = data_len < out_len - 1 ? data_len : out_len - 1;
    memcpy(out, data + data_start + data_offset, n);
    out[n] = '\0';
    return 0;
}

int akeno_param_sfo_title(const unsigned char *data, size_t len, const char *expected_title_id, char *out, size_t out_len) {
    out[0] = '\0';
    if (len < 20 || memcmp(data, "\0PSF", 4) != 0) return -1;
    uint32_t key_start = le32(data + 8);
    uint32_t data_start = le32(data + 12);
    uint32_t entries = le32(data + 16);
    if (entries > 1024 || 20 + (uint64_t)entries * 16 > len || key_start > len || data_start > len) return -1;
    char title[512] = "";
    char title_id[32] = "";
    for (uint32_t i = 0; i < entries; i++) {
        const unsigned char *entry = data + 20 + (size_t)i * 16;
        uint32_t key_offset = (uint32_t)key_start + le16(entry);
        if (key_offset >= len) return -1;
        const char *key = (const char *)data + key_offset;
        size_t key_max = len - key_offset;
        if (!memchr(key, 0, key_max)) return -1; /* Unterminated key. */
        if (strcmp(key, "TITLE") == 0) sfo_string(data, len, data_start, entry, title, sizeof(title));
        else if (strcmp(key, "TITLE_ID") == 0) sfo_string(data, len, data_start, entry, title_id, sizeof(title_id));
    }
    if (title_id[0] && expected_title_id && strcmp(title_id, expected_title_id) != 0) return -1;
    return take_name(title, out, out_len);
}

/* Reads a regular file of at most max bytes. Returns a malloc'd buffer (NUL-terminated) or NULL. */
static char *read_small_file(const char *path, size_t max, size_t *len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    char *data = NULL;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && (size_t)st.st_size <= max) {
        data = malloc((size_t)st.st_size + 1);
        size_t got = 0;
        while (data && got < (size_t)st.st_size) {
            ssize_t n = read(fd, data + got, (size_t)st.st_size - got);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            got += (size_t)n;
        }
        if (data && got == (size_t)st.st_size) {
            data[got] = '\0';
            *len = got;
        } else {
            free(data);
            data = NULL;
        }
    }
    close(fd);
    return data;
}

/*
 * Candidate metadata locations, tried in order. They are where the console keeps per-title
 * metadata copies and where an installed title's package is mounted while it runs. None of them
 * is documented by Sony; which ones a payload can read on a given firmware is a hardware
 * verification item. A miss only means the name stays unknown.
 */
static const struct {
    const char *dir;    /* Below the root, before the title ID. */
    const char *suffix; /* After the title ID. */
    int sfo;
} META_PATHS[] = {
    { "/system_data/priv/appmeta/", "/param.json", 0 },
    { "/system_data/priv/appmeta/", "/param.sfo", 1 },
    { "/user/appmeta/", "/param.json", 0 },
    { "/user/appmeta/", "/param.sfo", 1 },
    { "/mnt/sandbox/pfsmnt/", "-app0/sce_sys/param.json", 0 },
    { "/mnt/sandbox/pfsmnt/", "-app0/sce_sys/param.sfo", 1 },
};

int akeno_game_meta_lookup(const char *root, const char *title_id, char *name, size_t name_len, char *source, size_t source_len) {
    name[0] = '\0';
    if (source_len) source[0] = '\0';
    if (!root || !akeno_is_game_title_id(title_id)) return -1; /* Also keeps the path free of traversal. */
    for (size_t i = 0; i < sizeof(META_PATHS) / sizeof(META_PATHS[0]); i++) {
        char path[600];
        int n = snprintf(path, sizeof(path), "%s%s%s%s", root, META_PATHS[i].dir, title_id, META_PATHS[i].suffix);
        if (n < 0 || (size_t)n >= sizeof(path)) continue; /* Never open a truncated path. */
        size_t len = 0;
        char *data = read_small_file(path, META_PATHS[i].sfo ? PARAM_SFO_MAX_BYTES : PARAM_JSON_MAX_BYTES, &len);
        if (!data) continue;
        int rc = META_PATHS[i].sfo ? akeno_param_sfo_title((const unsigned char *)data, len, title_id, name, name_len)
                                   : akeno_param_json_title(data, len, title_id, name, name_len);
        free(data);
        if (rc == 0) {
            if (source_len) snprintf(source, source_len, "%s", path + strlen(root));
            return 0;
        }
    }
    return -1;
}

/* Icon locations: the per-title metadata folders (read-only); the running package is not used. */
static const char *const ICON_DIRS[] = { "/user/appmeta/", "/system_data/priv/appmeta/" };

unsigned char *akeno_game_icon_lookup(const char *root, const char *title_id, size_t *len, char *source, size_t source_len) {
    static const unsigned char png_signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    *len = 0;
    if (source_len) source[0] = '\0';
    if (!root || !akeno_is_game_title_id(title_id)) return NULL; /* Also keeps the path free of traversal. */
    for (size_t i = 0; i < sizeof(ICON_DIRS) / sizeof(ICON_DIRS[0]); i++) {
        char path[600];
        int n = snprintf(path, sizeof(path), "%s%s%s/icon0.png", root, ICON_DIRS[i], title_id);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        size_t got = 0;
        char *data = read_small_file(path, AKENO_ICON_MAX_BYTES, &got);
        if (!data) continue;
        if (got < sizeof(png_signature) || memcmp(data, png_signature, sizeof(png_signature)) != 0) {
            free(data);
            continue;
        }
        *len = got;
        if (source_len) snprintf(source, source_len, "%s", path + strlen(root));
        return (unsigned char *)data;
    }
    return NULL;
}
