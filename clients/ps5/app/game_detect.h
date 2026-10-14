/*
 * Game detection for the console app: pure logic, shared by the PS5 build and the Linux test build.
 *
 * The platform layer lists processes with the application info the system reports for them
 * (title ID, app ID, app type). This module decides, conservatively, whether exactly one game
 * is running, debounces the result over several scans, and reads the game's name from the
 * console's own metadata files. Nothing here touches another process: the inputs are read-only
 * process listings and files.
 *
 * What "detected" means: a process of a game title (PPSA..... or CUSA.....) exists and is not
 * stopped. Whether that game has the input focus cannot be determined with the interfaces this
 * app uses (see docs/GAME_ACTIVITY.md): it is reported as "running", never as "in focus".
 * Background system processes (NPXS.....) never count.
 */
#ifndef AKENO_GAME_DETECT_H
#define AKENO_GAME_DETECT_H

#include <stddef.h>

#define AKENO_TITLE_ID_MAX 16
/* Matches GAME_ACTIVITY.titleNameMax on the server (128), plus the terminator. */
#define AKENO_TITLE_NAME_MAX 129
#define AKENO_SCAN_MAX_CANDIDATES 4

typedef enum { AKENO_PROC_ACTIVE = 0, AKENO_PROC_STOPPED = 1 } akeno_proc_state;

/*
 * sceKernelGetAppInfo fills a structure that Sony does not document. The pinned SDK's samples
 * disagree on its layout: samples/ps reads the title ID at offset 20 (after an app_type field),
 * samples/test_privileges at offset 16. AKENO does not guess: it reads the raw bytes into a
 * zeroed, oversized buffer and records what looks like a title ID ([A-Z]{4}[0-9]{5}, NUL-
 * terminated) at either offset. A real title ID can only match at one of the two offsets, so the
 * console's own answers decide the layout (see akeno_game_classify).
 */
#define AKENO_APPINFO_OFFSET_TITLE_PS 20  /* samples/ps */
#define AKENO_APPINFO_OFFSET_TITLE_ALT 16 /* samples/test_privileges */
#define AKENO_APPINFO_RAW_HEAD 32         /* Bytes kept per process for the diagnostics probe. */

typedef struct {
    int pid;
    akeno_proc_state state;
    int has_app_info;                       /* The system returned application info for this process. */
    unsigned int app_id;
    char title_id[AKENO_TITLE_ID_MAX];      /* Title ID at the samples/ps offset, "" if none is there. */
    char title_id_alt[AKENO_TITLE_ID_MAX];  /* Title ID at the samples/test_privileges offset, "" if none. */
    char comm[24];
    unsigned char raw_head[AKENO_APPINFO_RAW_HEAD]; /* First bytes of the raw application info. */
} akeno_proc_info;

typedef enum {
    AKENO_SCAN_NONE = 0,   /* No running game process. */
    AKENO_SCAN_GAME,       /* Exactly one game title has running processes. */
    AKENO_SCAN_AMBIGUOUS,  /* More than one game title: nothing is reported. */
    AKENO_SCAN_FAILED,     /* The process list could not be read: nothing is reported. */
    AKENO_SCAN_UNVERIFIED  /* Title IDs appeared at both layout offsets: nothing is reported. */
} akeno_scan_kind;

typedef enum {
    AKENO_LAYOUT_UNKNOWN = 0, /* No title ID seen at either offset (nothing to decide from). */
    AKENO_LAYOUT_PS,          /* Title IDs only at offset 20. */
    AKENO_LAYOUT_ALT,         /* Title IDs only at offset 16. */
    AKENO_LAYOUT_CONFLICT     /* Title IDs at both offsets: the layout is not trusted. */
} akeno_appinfo_layout;

#define AKENO_SCAN_PROBES 4

typedef struct {
    akeno_scan_kind kind;
    char title_id[AKENO_TITLE_ID_MAX];
    int processes;            /* Processes listed. */
    int app_processes;        /* Processes with application info and a title ID. */
    int stopped_games;        /* Game processes ignored because they are stopped. */
    int candidates;           /* Distinct game titles with running processes. */
    char candidate_ids[AKENO_SCAN_MAX_CANDIDATES][AKENO_TITLE_ID_MAX];
    akeno_appinfo_layout layout;
    int layout_votes_ps;      /* Processes with a title ID at offset 20. */
    int layout_votes_alt;     /* Processes with a title ID at offset 16. */
    int probe_count;
    char probes[AKENO_SCAN_PROBES][96]; /* "pid comm: hex of the first raw bytes" */
    char error[120];
} akeno_game_scan;

/*
 * Fills app_id, title_id, title_id_alt and raw_head of out from the raw bytes the system wrote
 * (len bytes, the rest of the caller's zeroed buffer). Never reads outside raw[0..len).
 */
void akeno_appinfo_parse(const unsigned char *raw, size_t len, akeno_proc_info *out);

/* 1 when s is shaped like any PlayStation title ID ([A-Z]{4}[0-9]{5}), games or not. */
int akeno_looks_like_title_id(const char *s);

const char *akeno_layout_name(akeno_appinfo_layout layout);

/* 1 for PlayStation game title IDs: PPSA (PS5) or CUSA (PS4) followed by five digits. */
int akeno_is_game_title_id(const char *title_id);

const char *akeno_scan_kind_name(akeno_scan_kind kind);

/*
 * Classifies one process listing. self_pid (this app) is ignored. The layout is decided from this
 * listing first (see akeno_appinfo_layout); with a conflicting layout nothing is reported.
 */
void akeno_game_classify(const akeno_proc_info *procs, size_t count, int self_pid, akeno_game_scan *scan);

/*
 * A change is accepted only after `confirm` consecutive scans agree, so short-lived processes,
 * game switches and listing glitches do not create sessions. A failed or ambiguous scan counts
 * as "no game": AKENO never claims activity it cannot see.
 */
typedef struct {
    char current[AKENO_TITLE_ID_MAX]; /* Confirmed game, "" for none. */
    char pending[AKENO_TITLE_ID_MAX];
    int pending_seen;
} akeno_game_debounce;

/* Returns 1 when the confirmed game changed. */
int akeno_game_debounce_update(akeno_game_debounce *debounce, const akeno_game_scan *scan, int confirm);

/* ---- Title names from local metadata ------------------------------------------------------ */

/* Cleans a title name in place: valid UTF-8 only, control characters become spaces, surrounding
 * space is trimmed, and the result is cut to max_bytes on a character boundary. Returns 0 when
 * a non-empty name remains. */
int akeno_clean_title_name(char *name, size_t max_bytes);

/* Extracts the title name from a PS5 param.json. Returns 0 on success. When the file names a
 * different title ID than expected_title_id, it is rejected. */
int akeno_param_json_title(const char *data, size_t len, const char *expected_title_id, char *out, size_t out_len);

/* Extracts the TITLE entry from a PS4-format param.sfo. Returns 0 on success. */
int akeno_param_sfo_title(const unsigned char *data, size_t len, const char *expected_title_id, char *out, size_t out_len);

/*
 * Looks for the game's metadata under root ("" on the console) in the candidate locations listed
 * in game_detect.c and returns the first usable name. source receives the file that was used.
 * Files are opened read-only with size limits. Returns 0 when a name was found.
 */
int akeno_game_meta_lookup(const char *root, const char *title_id, char *name, size_t name_len, char *source, size_t source_len);

/*
 * Reads the icon the console keeps for an installed title (icon0.png in the same per-title
 * metadata folders as param.json/param.sfo), read-only. The file must be a PNG of at most
 * AKENO_ICON_MAX_BYTES. Returns a malloc'd buffer (release with free()) and its length, and the
 * path it came from (relative to root) in source, or NULL when no readable icon exists. The icon
 * is only shown on this console's own page; it is never sent to the AKENO server.
 */
#define AKENO_ICON_MAX_BYTES (1024 * 1024)
unsigned char *akeno_game_icon_lookup(const char *root, const char *title_id, size_t *len, char *source, size_t source_len);

#endif
