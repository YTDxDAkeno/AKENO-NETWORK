/*
 * What the Updates screen (Settings -> Updates) shows, from a snapshot of the updater's state.
 * Portable and unit-tested; src/app/update_app.c fills the snapshot, src/app/screens.c draws it.
 */
#ifndef AKN_UPDATE_VIEW_H
#define AKN_UPDATE_VIEW_H

#include <stddef.h>

#include "update_engine.h"
#include "update_install.h"
#include "update_prefs.h"

typedef enum {
    AKN_UPD_IDLE = 0,
    AKN_UPD_CHECKING,
    AKN_UPD_DOWNLOADING,
    AKN_UPD_VERIFYING,
    AKN_UPD_UNPACKING,
    AKN_UPD_INSTALLING,
    AKN_UPD_INSTALLED,     /* restart to use it */
    AKN_UPD_FAILED,        /* the last download or install failed (error) */
} akn_upd_phase;

typedef enum {
    AKN_UPD_ROW_CURRENT = 0,
    AKN_UPD_ROW_CHANNEL,
    AKN_UPD_ROW_CHECK,
    AKN_UPD_ROW_AUTO,
    AKN_UPD_ROW_LATEST,
    AKN_UPD_ROW_CHANGELOG,
    AKN_UPD_ROW_INSTALL,
    AKN_UPD_ROW_RESTART,
    AKN_UPD_ROW_HISTORY,
    AKN_UPD_ROW_COUNT
} akn_upd_row;

typedef struct {
    const char *own_version;
    const char *build;
    int has_root;                 /* started from the launcher folder with --install-dir */
    akn_boot_kind boot;
    int boot_attempt;
    char failed_version[AKN_SEMVER_TEXT_MAX];
    int confirmed_now;            /* this start confirmed a newly installed version */
    akn_update_prefs prefs;
    akn_upd_phase phase;
    int checking;
    int checked;                  /* a check finished in this session */
    akn_check_result result;
    char version[AKN_SEMVER_TEXT_MAX];
    int prerelease;
    char published[16];           /* "2026-10-18" */
    long long size;
    int previously_failed;
    char why[300];
    long long done, total;
    double bytes_per_s;
    char installed_version[AKN_SEMVER_TEXT_MAX];
    char error[300];
    char history_last[200];
    long long now_unix;
    int has_key;
} akn_upd_view;

/* The row's title and second line. */
void akn_upd_row_text(const akn_upd_view *v, akn_upd_row row, char *title, size_t title_len, char *sub, size_t sub_len);

/* Whether Cross does something on the row now. */
int akn_upd_row_enabled(const akn_upd_view *v, akn_upd_row row);

/* "Downloading 2.1 of 4.2 MB - 850 KB/s - 2.1 MB left" and similar; "" when idle. */
void akn_upd_progress_text(const akn_upd_view *v, char *out, size_t out_len);

/* The share of the download done (0..1), or -1 when not downloading. */
double akn_upd_progress(const akn_upd_view *v);

/* "5 minutes ago", "2 hours ago", "never". */
void akn_upd_ago(long long then_unix, long long now_unix, char *out, size_t out_len);

#endif
