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
 * app uses (see docs/GAME_ACTIVITY.md); background system processes never count.
 */
#ifndef AKENO_GAME_DETECT_H
#define AKENO_GAME_DETECT_H

#include <stddef.h>

#define AKENO_TITLE_ID_MAX 16
/* Matches GAME_ACTIVITY.titleNameMax on the server (128), plus the terminator. */
#define AKENO_TITLE_NAME_MAX 129
#define AKENO_SCAN_MAX_CANDIDATES 4

typedef enum { AKENO_PROC_ACTIVE = 0, AKENO_PROC_STOPPED = 1 } akeno_proc_state;

typedef struct {
    int pid;
    akeno_proc_state state;
    int has_app_info;               /* The system returned application info for this process. */
    unsigned int app_id;
    unsigned int app_type;
    char title_id[AKENO_TITLE_ID_MAX];
    char comm[24];
    int alt_title_layout;           /* Diagnostics only: see platform_ps5.c. */
} akeno_proc_info;

typedef enum {
    AKENO_SCAN_NONE = 0,   /* No running game process. */
    AKENO_SCAN_GAME,       /* Exactly one game title has running processes. */
    AKENO_SCAN_AMBIGUOUS,  /* More than one game title: nothing is reported. */
    AKENO_SCAN_FAILED      /* The process list could not be read: nothing is reported. */
} akeno_scan_kind;

typedef struct {
    akeno_scan_kind kind;
    char title_id[AKENO_TITLE_ID_MAX];
    int processes;            /* Processes listed. */
    int app_processes;        /* Processes with application info and a title ID. */
    int stopped_games;        /* Game processes ignored because they are stopped. */
    int candidates;           /* Distinct game titles with running processes. */
    char candidate_ids[AKENO_SCAN_MAX_CANDIDATES][AKENO_TITLE_ID_MAX];
    int alt_layout_hits;
    char error[120];
} akeno_game_scan;

/* 1 for PlayStation game title IDs: PPSA (PS5) or CUSA (PS4) followed by five digits. */
int akeno_is_game_title_id(const char *title_id);

const char *akeno_scan_kind_name(akeno_scan_kind kind);

/* Classifies one process listing. self_pid (this app) is ignored. */
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

#endif
