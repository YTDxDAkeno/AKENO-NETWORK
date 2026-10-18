/*
 * The native updater's policy (portable, unit-tested; docs/PS5_NATIVE_UPDATER.md):
 *
 *   1. GitHub releases of the official repository are read (update_net.c fetches them);
 *      akn_update_pick() chooses the newest compatible release of the user's channel.
 *   2. The release must carry three assets with fixed names whose download URLs point into the
 *      official repository: the update manifest, its signature and the package ZIP.
 *   3. The manifest is verified with the public key compiled into the app (ECDSA P-256 over
 *      SHA-256, update_verify.c) before anything in it is used; it names the package's SHA-256
 *      and size and every file's path, size and SHA-256, and states which firmware, daemon
 *      protocol and installed versions it supports.
 *   4. The package is checked against the manifest (update_zip.c) and installed side by side
 *      with the running version (update_install.c); nothing from the package is ever executed
 *      by the updater.
 *
 * Nothing here touches the network or the file system.
 */
#ifndef AKN_UPDATE_H
#define AKN_UPDATE_H

#include <stddef.h>
#include <stdint.h>

#include "semver.h"

#define AKN_UPDATE_REPO "YTDxDAkeno/AKENO-NETWORK"
#define AKN_UPDATE_API_URL "https://api.github.com/repos/" AKN_UPDATE_REPO "/releases?per_page=30"
#define AKN_UPDATE_DOWNLOAD_PREFIX "https://github.com/" AKN_UPDATE_REPO "/releases/download/"
#define AKN_UPDATE_TAG_PREFIX "native-"
#define AKN_UPDATE_MANIFEST_ASSET "akeno-native-update.json"
#define AKN_UPDATE_SIGNATURE_ASSET "akeno-native-update.json.sig"
#define AKN_UPDATE_PRODUCT "akeno-network-native"

#define AKN_UPDATE_RELEASES_MAX_BYTES (2u * 1024u * 1024u) /* the releases list */
#define AKN_UPDATE_MANIFEST_MAX_BYTES (64u * 1024u)
#define AKN_UPDATE_SIGNATURE_MAX_BYTES 512u
#define AKN_UPDATE_PACKAGE_MAX_BYTES (96u * 1024u * 1024u)
#define AKN_UPDATE_FILE_MAX_BYTES (64u * 1024u * 1024u)
#define AKN_UPDATE_FILES_MAX 8
#define AKN_UPDATE_NOTES_MAX 8192

typedef enum { AKN_CHANNEL_STABLE = 0, AKN_CHANNEL_BETA = 1 } akn_update_channel;

const char *akn_update_channel_name(akn_update_channel channel);

/* Where releases come from. The app uses the official values; tests point them at a mock. */
typedef struct {
    const char *api_url;          /* the releases list (JSON array) */
    const char *download_prefix;  /* every asset URL must be <prefix><tag>/<asset name> */
} akn_update_source;

const akn_update_source *akn_update_official_source(void);

typedef struct {
    char url[384];
    long long size;
} akn_update_asset;

/* The release chosen by akn_update_pick. */
typedef struct {
    char tag[80];
    char version[AKN_SEMVER_TEXT_MAX];
    char name[128];
    char published_at[32];
    char html_url[256];
    int prerelease;
    char notes[AKN_UPDATE_NOTES_MAX];   /* the release text (changelog), truncated */
    char package_name[96];              /* akeno-native-<version>.zip */
    akn_update_asset manifest, signature, package;
} akn_update_release;

typedef enum {
    AKN_PICK_FOUND = 0,       /* *out is a newer compatible release */
    AKN_PICK_UP_TO_DATE,      /* no newer release for this channel */
    AKN_PICK_ERROR            /* the list could not be read; see why */
} akn_pick_result;

/*
 * Chooses the newest release newer than current_version for the channel. Drafts, tags without the
 * prefix or with an invalid version, releases whose assets are missing or point anywhere but
 * <download_prefix><tag>/, and (on the stable channel) pre-releases are skipped. skipped counts
 * newer releases that were refused (reasons in why, the last one).
 */
akn_pick_result akn_update_pick(const akn_update_source *src, const char *json, size_t len, const char *current_version,
                                akn_update_channel channel, akn_update_release *out, int *skipped, char *why,
                                size_t why_len);

typedef struct {
    char path[64];        /* relative path inside the launcher folder, from the allow-list */
    long long size;
    char sha256[65];      /* lower-case hex */
} akn_update_file;

typedef struct {
    char version[AKN_SEMVER_TEXT_MAX];
    char tag[80];
    char channel[16];
    char package_name[96];
    long long package_size;
    char package_sha256[65];
    akn_update_file files[AKN_UPDATE_FILES_MAX];
    int file_count;
    /* Compatibility */
    char firmware_min[8], firmware_max[8]; /* "12.00"; "" = no bound */
    int daemon_protocol_min, daemon_protocol_max; /* 0 = no bound */
    char min_installed[AKN_SEMVER_TEXT_MAX];      /* "" = any */
    char notes[AKN_UPDATE_NOTES_MAX];
} akn_update_manifest;

/* The paths a package may contain, and only these (no scripts other than the launcher entry). */
int akn_update_path_allowed(const char *path);

/*
 * Parses an already signature-verified manifest and checks it against the release it came with
 * (tag, version, package name). Returns 0, or -1 with the reason in why.
 */
int akn_update_manifest_parse(const char *json, size_t len, const akn_update_release *release,
                              akn_update_manifest *out, char *why, size_t why_len);

/* What the console and the running app are. daemon_protocol: -1 when unknown. */
typedef struct {
    const char *current_version;
    uint32_t firmware_raw;        /* 0x12200000 = 12.20; 0 = unknown */
    int daemon_protocol;
} akn_update_env;

typedef enum {
    AKN_COMPAT_OK = 0,
    AKN_COMPAT_NOT_NEWER,
    AKN_COMPAT_FIRMWARE,
    AKN_COMPAT_DAEMON,
    AKN_COMPAT_FROM_VERSION
} akn_compat;

/* Whether the manifest's update may be installed here; why says what is needed otherwise. */
akn_compat akn_update_compatible(const akn_update_manifest *m, const akn_update_env *env, char *why, size_t why_len);

/* "12.20" -> 0x1220 (BCD major.minor), -1 when malformed. */
int akn_update_firmware_code(const char *text);

/* Lower-case hex of 32 bytes. */
void akn_update_hex(const uint8_t digest[32], char out[65]);

/* Bytes as "12.4 MB" / "830 KB". */
void akn_update_format_size(long long bytes, char *out, size_t out_len);

#endif
