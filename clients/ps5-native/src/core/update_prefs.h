/*
 * The updater's settings, in AKENO Native's own data folder (<native_dir>/updater.json, never in
 * the daemon's folder): the channel, whether to check automatically, and when the last check ran.
 */
#ifndef AKN_UPDATE_PREFS_H
#define AKN_UPDATE_PREFS_H

#include "update.h"

#define AKN_UPDATE_AUTO_INTERVAL_S (6 * 60 * 60)

typedef struct {
    akn_update_channel channel;
    int auto_check;            /* default on: checks only, never installs on its own */
    long long last_check;      /* Unix time of the last completed check, 0 = never */
    char last_seen[AKN_SEMVER_TEXT_MAX]; /* the newest version the user was told about */
} akn_update_prefs;

/* Defaults when the file is missing or unusable; a beta build defaults to the beta channel. */
void akn_update_prefs_defaults(akn_update_prefs *p, const char *own_version);
void akn_update_prefs_load(const char *dir, const char *own_version, akn_update_prefs *p);
int akn_update_prefs_save(const char *dir, const akn_update_prefs *p);

/* Whether an automatic check is due (auto on and the interval passed since the last one). */
int akn_update_prefs_due(const akn_update_prefs *p, long long now_unix);

#endif
