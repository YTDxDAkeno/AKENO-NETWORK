/*
 * A deliberately small ZIP reader for update packages (update.h). It accepts only what the
 * release workflow produces and refuses everything else:
 *
 *   - one disk, no ZIP64, no encryption, at most AKN_ZIP_MAX_ENTRIES entries, stored or DEFLATE;
 *   - names: relative, [A-Za-z0-9._-] components separated by "/", no "." or "..", no
 *     backslashes, drive letters, empty components or absolute paths;
 *   - no symbolic links or other special files, no duplicate names, no overlapping data;
 *   - the local header must agree with the central directory; CRC-32 is checked;
 *   - every size is bounded before anything is allocated.
 *
 * akn_zip_extract_package() then requires the archive to contain exactly the files the signed
 * manifest lists (with their sizes and SHA-256) and writes them below a staging directory.
 */
#ifndef AKN_UPDATE_ZIP_H
#define AKN_UPDATE_ZIP_H

#include <stddef.h>
#include <stdint.h>

#include "update.h"

#define AKN_ZIP_MAX_ENTRIES 32
#define AKN_ZIP_NAME_MAX 63

typedef struct {
    char name[AKN_ZIP_NAME_MAX + 1];
    int is_dir;
    uint16_t method;          /* 0 stored, 8 deflate */
    uint32_t crc32;
    uint32_t csize, usize;
    uint32_t data_offset;     /* where the entry's data starts */
} akn_zip_entry;

typedef struct {
    const uint8_t *data;
    size_t len;
    akn_zip_entry entries[AKN_ZIP_MAX_ENTRIES];
    int count;
} akn_zip;

/* Validates the archive's structure (no data is decompressed). 0, or -1 with the reason. */
int akn_zip_open(akn_zip *zip, const uint8_t *data, size_t len, char *why, size_t why_len);

/* Decompresses one entry into out (exactly entry->usize bytes, CRC-checked). 0 or -1. */
int akn_zip_read(const akn_zip *zip, const akn_zip_entry *entry, uint8_t *out, size_t out_len, char *why, size_t why_len);

/* Whether a name is a safe relative path (the rules above; directories end with "/"). */
int akn_zip_name_safe(const char *name, size_t len);

/*
 * Checks the package against the manifest and writes the files below dest_dir (which must exist):
 * every listed file present with its size and SHA-256, nothing else besides the directories of
 * listed files. Files are written to a temporary name, synced and renamed. cancel (may be NULL) is
 * polled between files. 0, or -1 with the reason.
 */
int akn_zip_extract_package(const uint8_t *data, size_t len, const akn_update_manifest *m, const char *dest_dir,
                            volatile int *cancel, char *why, size_t why_len);

#endif
