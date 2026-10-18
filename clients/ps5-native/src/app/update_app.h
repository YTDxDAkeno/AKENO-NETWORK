/*
 * Updates in AKENO Native (docs/PS5_NATIVE_UPDATER.md): the start-up bookkeeping (trial starts,
 * rollback, confirmation), the check and install worker thread, automatic checks and the
 * "update available" question. The engine is src/core/update_engine.c; nothing here writes
 * outside the launcher folder's update/ directory and AKENO Native's own data folder.
 */
#ifndef AKN_UPDATE_APP_H
#define AKN_UPDATE_APP_H

#include "app.h"

/* Early at start, after the log is open: --install-dir, trial starts and rollback, settings. */
void akn_upd_init(akn_app *app);
/* Every frame: finished jobs, confirmation of a new version, automatic checks, the question. */
void akn_upd_poll(akn_app *app, uint64_t now);
/* 0 when started; -1 with a toast when something runs already. */
int akn_upd_check(akn_app *app, int automatic, char *why, size_t why_len);
int akn_upd_install(akn_app *app, char *why, size_t why_len);
void akn_upd_cancel(akn_app *app);
void akn_upd_set_channel(akn_app *app, akn_update_channel channel);
void akn_upd_set_auto(akn_app *app, int on);
/* A consistent copy of the state for the screen; notes (may be NULL) receive the changelog. */
void akn_upd_view_get(akn_app *app, akn_upd_view *v, char *notes, size_t notes_len, char *history, size_t history_len);
/* Stops a running job and waits for it (bounded by the download timeouts). */
void akn_upd_shutdown(akn_app *app);

#endif
