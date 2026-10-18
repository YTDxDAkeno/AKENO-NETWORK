#include "update_zip.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png.h"
#include "stb_image_akn.h"
#include "update_fs.h"
#include "update_verify.h"

#define SIG_EOCD 0x06054b50u
#define SIG_CDIR 0x02014b50u
#define SIG_LOCAL 0x04034b50u
#define EOCD_SIZE 22u
#define CDIR_SIZE 46u
#define LOCAL_SIZE 30u
#define MAX_RATIO 200u /* uncompressed/compressed; the release's files compress by far less */

#define WHY(...)                                                 \
    do {                                                         \
        if (why && why_len) snprintf(why, why_len, __VA_ARGS__); \
    } while (0)

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int akn_zip_name_safe(const char *name, size_t len) {
    if (len == 0 || len > AKN_ZIP_NAME_MAX) return 0;
    if (name[0] == '/') return 0;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || name[i] == '/') {
            size_t n = i - start;
            if (n == 0) return i == len && i > 0 && name[i - 1] == '/' && len > 1; /* a trailing "/" ends a directory */
            if ((n == 1 && name[start] == '.') || (n == 2 && name[start] == '.' && name[start + 1] == '.')) return 0;
            start = i + 1;
            continue;
        }
        char c = name[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                 c == '-';
        if (!ok) return 0;
    }
    return 1;
}

int akn_zip_open(akn_zip *zip, const uint8_t *data, size_t len, char *why, size_t why_len) {
    memset(zip, 0, sizeof(*zip));
    zip->data = data;
    zip->len = len;
    if (len < EOCD_SIZE || len > 0xFFFFFFFFu) {
        WHY("The package is not a ZIP archive (size).");
        return -1;
    }
    /* The end record: searched backwards over at most the longest possible comment. */
    size_t eocd = 0;
    int found = 0;
    size_t lowest = len > EOCD_SIZE + 0xFFFFu ? len - EOCD_SIZE - 0xFFFFu : 0;
    for (size_t pos = len - EOCD_SIZE + 1; pos-- > lowest;) {
        if (rd32(data + pos) == SIG_EOCD && pos + EOCD_SIZE + rd16(data + pos + 20) == len) {
            eocd = pos;
            found = 1;
            break;
        }
    }
    if (!found) {
        WHY("The package is not a ZIP archive (no end record).");
        return -1;
    }
    const uint8_t *e = data + eocd;
    uint16_t disk = rd16(e + 4), cd_disk = rd16(e + 6), here = rd16(e + 8), total = rd16(e + 10);
    uint32_t cd_size = rd32(e + 12), cd_off = rd32(e + 16);
    if (disk != 0 || cd_disk != 0 || here != total) {
        WHY("The package spans several disks.");
        return -1;
    }
    if (total == 0xFFFF || cd_size == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu) {
        WHY("The package uses ZIP64, which is not accepted.");
        return -1;
    }
    if (total == 0 || total > AKN_ZIP_MAX_ENTRIES) {
        WHY("The package has %u entries (1 to %d accepted).", (unsigned)total, AKN_ZIP_MAX_ENTRIES);
        return -1;
    }
    if ((size_t)cd_off + cd_size != eocd) {
        WHY("The package's directory is not where its end record says.");
        return -1;
    }
    size_t p = cd_off;
    for (unsigned i = 0; i < total; i++) {
        if (p + CDIR_SIZE > eocd || rd32(data + p) != SIG_CDIR) {
            WHY("The package's directory is damaged (entry %u).", i + 1);
            return -1;
        }
        const uint8_t *c = data + p;
        uint16_t made_by = rd16(c + 4), flags = rd16(c + 8), method = rd16(c + 10);
        uint32_t crc = rd32(c + 16), csize = rd32(c + 20), usize = rd32(c + 24);
        uint16_t nlen = rd16(c + 28), xlen = rd16(c + 30), clen = rd16(c + 32), start_disk = rd16(c + 34);
        uint32_t ext_attr = rd32(c + 38), lho = rd32(c + 42);
        size_t next = p + CDIR_SIZE + nlen + xlen + clen;
        if (next > eocd) {
            WHY("The package's directory is damaged (entry %u).", i + 1);
            return -1;
        }
        const char *name = (const char *)c + CDIR_SIZE;
        if (!akn_zip_name_safe(name, nlen)) {
            WHY("The package contains an unsafe file name (entry %u).", i + 1);
            return -1;
        }
        if (flags & 0x0001u || flags & 0x0040u || flags & 0x2000u) {
            WHY("The package contains an encrypted file.");
            return -1;
        }
        if (method != 0 && method != 8) {
            WHY("The package uses an unsupported compression method (%u).", (unsigned)method);
            return -1;
        }
        if (start_disk != 0 || csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || lho == 0xFFFFFFFFu) {
            WHY("The package uses ZIP64 or several disks.");
            return -1;
        }
        unsigned mode = ext_attr >> 16;
        if ((made_by >> 8) == 3 && mode != 0 && (mode & 0170000u) != 0100000u && (mode & 0170000u) != 0040000u) {
            WHY("The package contains a link or special file.");
            return -1;
        }
        akn_zip_entry *en = &zip->entries[i];
        memcpy(en->name, name, nlen);
        en->name[nlen] = '\0';
        en->is_dir = name[nlen - 1] == '/';
        if (en->is_dir && (usize != 0 || csize != 0)) {
            WHY("The package contains a directory with data.");
            return -1;
        }
        if (method == 0 && csize != usize) {
            WHY("A stored file's sizes disagree.");
            return -1;
        }
        if (method == 8 && usize > 0 && (csize == 0 || usize / csize > MAX_RATIO)) {
            WHY("A file in the package is compressed implausibly well (refused).");
            return -1;
        }
        for (int k = 0; k < zip->count; k++)
            if (strcmp(zip->entries[k].name, en->name) == 0) {
                WHY("The package contains a file twice.");
                return -1;
            }
        /* The local header: same name, method and flags; data inside the archive, before the directory. */
        if ((size_t)lho + LOCAL_SIZE > cd_off || rd32(data + lho) != SIG_LOCAL) {
            WHY("A file's header in the package is damaged.");
            return -1;
        }
        const uint8_t *l = data + lho;
        uint16_t lflags = rd16(l + 6), lmethod = rd16(l + 8), lnlen = rd16(l + 26), lxlen = rd16(l + 28);
        if (lmethod != method || (lflags & 0x0001u) || lnlen != nlen || memcmp(l + LOCAL_SIZE, name, nlen) != 0) {
            WHY("A file's header disagrees with the package's directory.");
            return -1;
        }
        if (!(lflags & 0x0008u) && (rd32(l + 14) != crc || rd32(l + 18) != csize || rd32(l + 22) != usize)) {
            WHY("A file's sizes disagree with the package's directory.");
            return -1;
        }
        size_t data_off = (size_t)lho + LOCAL_SIZE + lnlen + lxlen;
        if (data_off > cd_off || (size_t)csize > cd_off - data_off) {
            WHY("A file's data lies outside the package.");
            return -1;
        }
        en->method = method;
        en->crc32 = crc;
        en->csize = csize;
        en->usize = usize;
        en->data_offset = (uint32_t)data_off;
        /* No two entries may share bytes (overlapping entries are a ZIP-bomb technique). */
        for (int k = 0; k < zip->count; k++) {
            const akn_zip_entry *o = &zip->entries[k];
            if (en->csize == 0 || o->csize == 0) continue;
            if (en->data_offset < o->data_offset + o->csize && o->data_offset < en->data_offset + en->csize) {
                WHY("Files in the package overlap.");
                return -1;
            }
        }
        zip->count++;
        p = next;
    }
    if (p != eocd) {
        WHY("The package's directory has trailing data.");
        return -1;
    }
    return 0;
}

int akn_zip_read(const akn_zip *zip, const akn_zip_entry *entry, uint8_t *out, size_t out_len, char *why, size_t why_len) {
    if (out_len != entry->usize) {
        WHY("Internal error: wrong buffer size.");
        return -1;
    }
    const uint8_t *src = zip->data + entry->data_offset;
    if (entry->method == 0) {
        memcpy(out, src, entry->usize);
    } else if (entry->usize > 0) {
        if (entry->csize > 0x7FFFFFFFu || entry->usize > 0x7FFFFFFFu) {
            WHY("A file in the package is too large.");
            return -1;
        }
        int n = akn_stbi_inflate_raw(out, (int)entry->usize, src, (int)entry->csize);
        if (n < 0 || (uint32_t)n != entry->usize) {
            WHY("A file in the package cannot be decompressed: %s.", entry->name);
            return -1;
        }
    }
    if (akn_crc32(0, out, entry->usize) != entry->crc32) {
        WHY("A file in the package is damaged (CRC-32): %s.", entry->name);
        return -1;
    }
    return 0;
}

static int write_part(const char *dest_dir, const char *name, const uint8_t *data, size_t len, char *why, size_t why_len) {
    char path[512];
    if (snprintf(path, sizeof(path), "%s/%s", dest_dir, name) >= (int)sizeof(path)) {
        WHY("The staging path is too long.");
        return -1;
    }
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    if (akn_fs_mkdirs(dir) != 0 || akn_fs_write_atomic(path, data, len) != 0) {
        const char *t = akn_fs_error_text(errno);
        WHY("Writing %s failed: %s", name, t ? t : strerror(errno));
        return -1;
    }
    return 0;
}

int akn_zip_extract_package(const uint8_t *data, size_t len, const akn_update_manifest *m, const char *dest_dir,
                            volatile int *cancel, char *why, size_t why_len) {
    akn_zip *zip = malloc(sizeof(*zip));
    if (!zip) {
        WHY("Out of memory.");
        return -1;
    }
    int rc = -1;
    if (akn_zip_open(zip, data, len, why, why_len) != 0) goto done;
    int seen[AKN_UPDATE_FILES_MAX] = { 0 };
    for (int i = 0; i < zip->count; i++) {
        const akn_zip_entry *en = &zip->entries[i];
        if (en->is_dir) {
            /* Only the directories of listed files ("sce_sys/", "daemon/"). */
            size_t n = strlen(en->name);
            int ok = 0;
            for (int f = 0; f < m->file_count; f++)
                if (strncmp(m->files[f].path, en->name, n) == 0) ok = 1;
            if (!ok) {
                WHY("The package contains a folder the update does not list: %s", en->name);
                goto done;
            }
            continue;
        }
        int idx = -1;
        for (int f = 0; f < m->file_count; f++)
            if (strcmp(m->files[f].path, en->name) == 0) idx = f;
        if (idx < 0) {
            WHY("The package contains a file the signed update does not list: %s", en->name);
            goto done;
        }
        if (seen[idx]) {
            WHY("The package contains a file twice.");
            goto done;
        }
        seen[idx] = 1;
        if ((long long)en->usize != m->files[idx].size) {
            WHY("%s does not have the signed size.", en->name);
            goto done;
        }
    }
    for (int f = 0; f < m->file_count; f++)
        if (!seen[f]) {
            WHY("The package lacks %s.", m->files[f].path);
            goto done;
        }
    for (int i = 0; i < zip->count; i++) {
        const akn_zip_entry *en = &zip->entries[i];
        if (en->is_dir) continue;
        if (cancel && *cancel) {
            WHY("Cancelled.");
            goto done;
        }
        int idx = -1;
        for (int f = 0; f < m->file_count; f++)
            if (strcmp(m->files[f].path, en->name) == 0) idx = f;
        uint8_t *buf = malloc(en->usize ? en->usize : 1);
        if (!buf) {
            WHY("Out of memory for %s.", en->name);
            goto done;
        }
        if (akn_zip_read(zip, en, buf, en->usize, why, why_len) != 0) {
            free(buf);
            goto done;
        }
        uint8_t digest[32];
        char hex[65];
        akn_sha256(buf, en->usize, digest);
        akn_update_hex(digest, hex);
        if (strcmp(hex, m->files[idx].sha256) != 0) {
            free(buf);
            WHY("%s does not match the signed checksum.", en->name);
            goto done;
        }
        int w = write_part(dest_dir, en->name, buf, en->usize, why, why_len);
        free(buf);
        if (w != 0) goto done;
    }
    rc = 0;
done:
    free(zip);
    return rc;
}
