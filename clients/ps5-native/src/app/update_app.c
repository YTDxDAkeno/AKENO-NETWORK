#include "update_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "update_verify.h"

#define AUTO_FIRST_DELAY_MS 8000u     /* the first automatic check, after the connection checks */
#define CONFIRM_AFTER_MS 20000u       /* a new version that ran this long confirms itself */
enum { JOB_CHECK = 1, JOB_INSTALL = 2 };

static const char *const k_check_name[] = { "up to date", "available", "incompatible", "unverified", "rejected", "failed" };

/* The download source and hosts: GitHub. PC builds can point at a test server (tests/updater). */
static void source_for(akn_update_source *src, akn_dl_config *dl, const char **key, char *key_buf, size_t key_buf_len) {
    *src = *akn_update_official_source();
    memset(dl, 0, sizeof(*dl));
    dl->hosts = akn_update_github_hosts();
    dl->user_agent = "AKENO-NETWORK-Native/" AKN_NATIVE_VERSION;
    *key = akn_update_trusted_key();
    (void)key_buf;
    (void)key_buf_len;
#ifndef AKN_PLATFORM_PS5
    static const char *hosts[] = { "127.0.0.1", "localhost", NULL };
    const char *api = getenv("AKENO_NATIVE_UPDATE_API"), *prefix = getenv("AKENO_NATIVE_UPDATE_PREFIX");
    if (api && prefix) {
        src->api_url = api;
        src->download_prefix = prefix;
        dl->hosts = hosts;
        dl->connect_timeout_ms = dl->idle_timeout_ms = 3000;
    }
    const char *key_file = getenv("AKENO_NATIVE_UPDATE_KEY_FILE");
    if (key_file) {
        FILE *f = fopen(key_file, "r");
        if (f) {
            size_t n = fread(key_buf, 1, key_buf_len - 1, f);
            key_buf[n] = '\0';
            fclose(f);
            *key = key_buf;
        }
    }
#endif
}

static int daemon_protocol(akn_app *app) {
    switch (app->social.ipc) {
    case AKN_IPC_UNCHECKED:
    case AKN_IPC_NO_DAEMON: return -1;
    case AKN_IPC_NOT_SUPPORTED: return 0;
    default: return app->social.daemon.ipc > 0 ? app->social.daemon.ipc : 0;
    }
}

typedef struct {
    akn_app *app;
    akn_install_phase phase;
} progress_ctx;

static void on_progress(void *ctx, akn_install_phase phase, long long done, long long total) {
    akn_app *app = ((progress_ctx *)ctx)->app;
    uint64_t now = akn_platform_now_ms();
    SDL_LockMutex(app->lock);
    app->upd.phase = phase == AKN_PHASE_DOWNLOAD ? AKN_UPD_DOWNLOADING
                     : phase == AKN_PHASE_VERIFY ? AKN_UPD_VERIFYING
                     : phase == AKN_PHASE_UNPACK ? AKN_UPD_UNPACKING
                                                 : AKN_UPD_INSTALLING;
    if (phase == AKN_PHASE_DOWNLOAD) {
        app->upd.done = done;
        app->upd.total = total;
        if (now - app->upd.speed_at >= 1000) {
            if (app->upd.speed_at) {
                double rate = (double)(done - app->upd.speed_done) * 1000.0 / (double)(now - app->upd.speed_at);
                app->upd.speed = app->upd.speed > 0 ? app->upd.speed * 0.6 + rate * 0.4 : rate;
            }
            app->upd.speed_at = now;
            app->upd.speed_done = done;
        }
    }
    SDL_UnlockMutex(app->lock);
}

static int worker(void *arg) {
    akn_app *app = arg;
    akn_update_source src;
    akn_updater_cfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    static char key_buf[1024];
    source_for(&src, &cfg.dl, &cfg.trusted_key, key_buf, sizeof(key_buf));
    cfg.source = &src;
    cfg.root = app->upd.root[0] ? app->upd.root : NULL;
    cfg.env.current_version = AKN_NATIVE_VERSION;
    cfg.env.firmware_raw = app->platform.firmware_raw;
    SDL_LockMutex(app->lock);
    cfg.env.daemon_protocol = daemon_protocol(app);
    cfg.channel = app->upd.prefs.channel;
    SDL_UnlockMutex(app->lock);
    cfg.own_elf = app->upd.boot.own_elf;

    if (app->upd.job == JOB_CHECK) {
        akn_check_outcome *o = calloc(1, sizeof(*o));
        if (o) akn_updater_check(&cfg, &app->upd.cancel, o);
        SDL_LockMutex(app->lock);
        if (o) {
            free(app->upd.outcome);
            app->upd.outcome = o;
            app->upd.checked = 1;
        }
        app->upd.checking = 0;
        SDL_UnlockMutex(app->lock);
        if (o)
            akn_logf(&app->log, AKN_LOG_INFO, "update.check", "%s channel: %s%s%s%s", akn_update_channel_name(cfg.channel),
                     k_check_name[o->result], o->release.version[0] ? " " : "", o->release.version, o->why[0] ? "" : "");
        if (o && o->why[0]) akn_logf(&app->log, AKN_LOG_INFO, "update.check", "%s", o->why);
    } else {
        akn_check_outcome *copy = malloc(sizeof(*copy));
        SDL_LockMutex(app->lock);
        if (copy && app->upd.outcome) *copy = *app->upd.outcome;
        int have = copy && app->upd.outcome;
        SDL_UnlockMutex(app->lock);
        char why[300] = "";
        progress_ctx pc = { app, AKN_PHASE_DOWNLOAD };
        akn_install_hooks hooks = { &app->upd.cancel, on_progress, &pc };
        int rc = have ? akn_updater_install(&cfg, copy, &hooks, why, sizeof(why)) : -1;
        if (!have) snprintf(why, sizeof(why), "There is no checked update to install.");
        SDL_LockMutex(app->lock);
        if (rc == 0) {
            app->upd.phase = AKN_UPD_INSTALLED;
            snprintf(app->upd.installed, sizeof(app->upd.installed), "%s", copy->manifest.version);
        } else {
            app->upd.phase = AKN_UPD_FAILED;
            snprintf(app->upd.error, sizeof(app->upd.error), "%s", why[0] ? why : "The update failed.");
        }
        SDL_UnlockMutex(app->lock);
        akn_logf(&app->log, rc == 0 ? AKN_LOG_INFO : AKN_LOG_WARN, "update.install", "%s %s: %s", rc == 0 ? "installed" : "not installed",
                 have ? copy->manifest.version : "-", rc == 0 ? "restart to use it" : why);
        free(copy);
    }
    app->upd.thread_done = 1;
    return 0;
}

static int start_job(akn_app *app, int job, char *why, size_t why_len) {
    if (app->upd.thread) {
        snprintf(why, why_len, "%s", "The updater is busy.");
        return -1;
    }
    app->upd.job = job;
    app->upd.cancel = 0;
    app->upd.thread_done = 0;
    app->upd.thread = SDL_CreateThread(worker, "akeno-update", app);
    if (!app->upd.thread) {
        snprintf(why, why_len, "%s", "The updater could not start.");
        return -1;
    }
    return 0;
}

void akn_upd_init(akn_app *app) {
    const char *dir = app->options.install_dir;
    if (dir && akn_update_root_valid(dir)) snprintf(app->upd.root, sizeof(app->upd.root), "%s", dir);
    else if (dir) akn_logf(&app->log, AKN_LOG_WARN, "update.root", "--install-dir %s is not a launcher folder: updates cannot be installed", dir);
    akn_update_prefs_load(app->platform.native_dir, AKN_NATIVE_VERSION, &app->upd.prefs);
    snprintf(app->upd.boot.own_elf, sizeof(app->upd.boot.own_elf), "%s", AKN_UPDATE_FACTORY_ELF);
    if (app->upd.root[0]) {
        akn_boot_kind k = akn_update_boot(app->upd.root, AKN_NATIVE_VERSION, &app->upd.boot);
        if (k == AKN_BOOT_TRIAL)
            akn_logf(&app->log, AKN_LOG_INFO, "update.boot", "new version %s, start %d of %d", AKN_NATIVE_VERSION, app->upd.boot.attempt,
                     AKN_UPDATE_MAX_ATTEMPTS);
        else if (k == AKN_BOOT_ROLLED_BACK) {
            akn_logf(&app->log, AKN_LOG_WARN, "update.boot", "rolled back: %s did not start", app->upd.boot.failed_version);
            char text[200];
            snprintf(text, sizeof(text), "AKENO NETWORK %s did not start, so AKENO went back to %s.", app->upd.boot.failed_version, AKN_NATIVE_VERSION);
            akn_platform_notify(text);
        }
    }
    app->upd.next_auto_ms = akn_platform_now_ms() + AUTO_FIRST_DELAY_MS;
}

int akn_upd_check(akn_app *app, int automatic, char *why, size_t why_len) {
    SDL_LockMutex(app->lock);
    int busy = app->upd.phase >= AKN_UPD_DOWNLOADING && app->upd.phase <= AKN_UPD_INSTALLING;
    SDL_UnlockMutex(app->lock);
    if (busy || app->upd.thread) {
        snprintf(why, why_len, "%s", "The updater is busy.");
        return -1;
    }
    SDL_LockMutex(app->lock);
    app->upd.checking = 1;
    app->upd.automatic = automatic;
    SDL_UnlockMutex(app->lock);
    if (start_job(app, JOB_CHECK, why, why_len) != 0) {
        SDL_LockMutex(app->lock);
        app->upd.checking = 0;
        SDL_UnlockMutex(app->lock);
        return -1;
    }
    return 0;
}

int akn_upd_install(akn_app *app, char *why, size_t why_len) {
    SDL_LockMutex(app->lock);
    int ok = app->upd.outcome && app->upd.outcome->result == AKN_CHECK_AVAILABLE;
    SDL_UnlockMutex(app->lock);
    if (!ok) {
        snprintf(why, why_len, "%s", "Check for updates first.");
        return -1;
    }
    if (!app->upd.root[0]) {
        snprintf(why, why_len, "%s", "Start AKENO from the Homebrew Launcher to install updates.");
        return -1;
    }
    SDL_LockMutex(app->lock);
    app->upd.phase = AKN_UPD_DOWNLOADING;
    app->upd.done = 0;
    app->upd.total = app->upd.outcome->manifest.package_size;
    app->upd.speed = 0;
    app->upd.speed_at = 0;
    app->upd.speed_done = 0;
    app->upd.error[0] = '\0';
    SDL_UnlockMutex(app->lock);
    if (start_job(app, JOB_INSTALL, why, why_len) != 0) {
        SDL_LockMutex(app->lock);
        app->upd.phase = AKN_UPD_IDLE;
        SDL_UnlockMutex(app->lock);
        return -1;
    }
    akn_logf(&app->log, AKN_LOG_INFO, "update.install", "started %s", app->upd.outcome->release.version);
    return 0;
}

void akn_upd_cancel(akn_app *app) { app->upd.cancel = 1; }

void akn_upd_set_channel(akn_app *app, akn_update_channel channel) {
    if (app->upd.prefs.channel == channel) return;
    app->upd.prefs.channel = channel;
    SDL_LockMutex(app->lock);
    free(app->upd.outcome); /* the old result belongs to the other channel */
    app->upd.outcome = NULL;
    app->upd.checked = 0;
    SDL_UnlockMutex(app->lock);
    akn_update_prefs_save(app->platform.native_dir, &app->upd.prefs);
}

void akn_upd_set_auto(akn_app *app, int on) {
    app->upd.prefs.auto_check = on ? 1 : 0;
    akn_update_prefs_save(app->platform.native_dir, &app->upd.prefs);
}

void akn_upd_view_get(akn_app *app, akn_upd_view *v, char *notes, size_t notes_len, char *history, size_t history_len) {
    memset(v, 0, sizeof(*v));
    v->own_version = AKN_NATIVE_VERSION;
    v->build = AKN_BUILD_ID;
    v->has_root = app->upd.root[0] != '\0';
    v->boot = app->upd.boot.kind;
    v->boot_attempt = app->upd.boot.attempt;
    snprintf(v->failed_version, sizeof(v->failed_version), "%s", app->upd.boot.failed_version);
    v->confirmed_now = app->upd.confirmed_now;
    v->prefs = app->upd.prefs;
    v->now_unix = akn_platform_unix_time();
    static int has_key = -1; /* the key never changes while AKENO runs */
    if (has_key < 0) {
        akn_update_source src;
        akn_dl_config dl;
        const char *key = NULL;
        static char key_buf[1024];
        source_for(&src, &dl, &key, key_buf, sizeof(key_buf));
        has_key = key != NULL;
    }
    v->has_key = has_key;
    if (notes && notes_len) notes[0] = '\0';
    SDL_LockMutex(app->lock);
    v->phase = app->upd.phase;
    v->checking = app->upd.checking;
    v->checked = app->upd.checked && app->upd.outcome;
    if (v->checked) {
        const akn_check_outcome *o = app->upd.outcome;
        v->result = o->result;
        snprintf(v->version, sizeof(v->version), "%s", o->release.version);
        v->prerelease = o->release.prerelease;
        snprintf(v->published, sizeof(v->published), "%.10s", o->release.published_at);
        v->size = o->manifest.package_size ? o->manifest.package_size : o->release.package.size;
        v->previously_failed = o->previously_failed;
        snprintf(v->why, sizeof(v->why), "%s", o->why);
        if (notes && notes_len) snprintf(notes, notes_len, "%s", o->release.notes);
    }
    v->done = app->upd.done;
    v->total = app->upd.total;
    v->bytes_per_s = app->upd.speed;
    snprintf(v->installed_version, sizeof(v->installed_version), "%s", app->upd.installed);
    snprintf(v->error, sizeof(v->error), "%s", app->upd.error);
    SDL_UnlockMutex(app->lock);
    if (app->upd.root[0]) {
        char text[4096];
        if (akn_update_history_read(app->upd.root, text, sizeof(text)) == 0 && text[0]) {
            if (history && history_len) snprintf(history, history_len, "%s", text);
            size_t n = strlen(text);
            while (n && text[n - 1] == '\n') text[--n] = '\0';
            const char *last = strrchr(text, '\n');
            snprintf(v->history_last, sizeof(v->history_last), "%.199s", last ? last + 1 : text);
        } else if (history && history_len) {
            history[0] = '\0';
        }
    } else if (history && history_len) {
        history[0] = '\0';
    }
}

void akn_upd_poll(akn_app *app, uint64_t now) {
    if (app->upd.thread && app->upd.thread_done) {
        SDL_WaitThread(app->upd.thread, NULL);
        app->upd.thread = NULL;
        if (app->upd.job == JOB_CHECK) {
            app->upd.prefs.last_check = akn_platform_unix_time();
            SDL_LockMutex(app->lock);
            int available = app->upd.outcome && app->upd.outcome->result == AKN_CHECK_AVAILABLE;
            char version[AKN_SEMVER_TEXT_MAX] = "", size[32] = "";
            if (available) {
                snprintf(version, sizeof(version), "%s", app->upd.outcome->release.version);
                akn_update_format_size(app->upd.outcome->manifest.package_size, size, sizeof(size));
            }
            int automatic = app->upd.automatic;
            SDL_UnlockMutex(app->lock);
            if (available) snprintf(app->upd.prefs.last_seen, sizeof(app->upd.prefs.last_seen), "%s", version);
            akn_update_prefs_save(app->platform.native_dir, &app->upd.prefs);
            /* An automatic check asks once per version and session, when no other dialog is open. */
            if (available && automatic && strcmp(app->upd.prompted, version) != 0 && app->ui.dialog == AKN_DIALOG_NONE) {
                snprintf(app->upd.prompted, sizeof(app->upd.prompted), "%s", version);
                char text[240];
                snprintf(text, sizeof(text), "A new AKENO NETWORK update is available. Install now? Version %s, %s. "
                         "Your data and sign-in are kept.", version, size);
                akn_ui_open_confirm(&app->ui, AKN_CONFIRM_UPDATE_INSTALL, "Update available", text, "Update");
                snprintf(app->ui.confirm_no, sizeof(app->ui.confirm_no), "Later");
            } else if (!automatic && !available && app->ui.screen != AKN_SCREEN_UPDATES) {
                SDL_LockMutex(app->lock);
                char msg[320];
                snprintf(msg, sizeof(msg), "%s", app->upd.outcome && app->upd.outcome->result == AKN_CHECK_UP_TO_DATE
                                                     ? "AKENO NETWORK is up to date."
                                                     : app->upd.outcome ? app->upd.outcome->why : "The check failed.");
                SDL_UnlockMutex(app->lock);
                akn_ui_toast(&app->ui, msg, now, 4000);
            }
        } else {
            SDL_LockMutex(app->lock);
            int ok = app->upd.phase == AKN_UPD_INSTALLED;
            char msg[320];
            snprintf(msg, sizeof(msg), ok ? "AKENO NETWORK %s is installed. Restart AKENO to use it." : "%s", ok ? app->upd.installed : app->upd.error);
            SDL_UnlockMutex(app->lock);
            akn_ui_toast(&app->ui, msg, now, 5000);
        }
    }
    /* A newly installed version that ran normally keeps itself. */
    if (app->upd.boot.kind == AKN_BOOT_TRIAL && !app->upd.confirmed_now && app->frames > 0 && now - app->started_ms >= CONFIRM_AFTER_MS) {
        app->upd.confirmed_now = 1;
        if (akn_update_confirm(app->upd.root, AKN_NATIVE_VERSION))
            akn_logf(&app->log, AKN_LOG_INFO, "update.confirm", "%s ran normally: kept", AKN_NATIVE_VERSION);
    }
    /* Automatic checks: at start (once the daemon was asked) and every 6 hours. */
    if (app->upd.prefs.auto_check && !app->upd.thread && now >= app->upd.next_auto_ms && app->social.ipc != AKN_IPC_UNCHECKED) {
        app->upd.next_auto_ms = now + (uint64_t)AKN_UPDATE_AUTO_INTERVAL_S * 1000u;
        char why[80];
        akn_upd_check(app, 1, why, sizeof(why));
    }
}

void akn_upd_shutdown(akn_app *app) {
    app->upd.cancel = 1;
    if (app->upd.thread) SDL_WaitThread(app->upd.thread, NULL);
    app->upd.thread = NULL;
    SDL_LockMutex(app->lock);
    free(app->upd.outcome);
    app->upd.outcome = NULL;
    SDL_UnlockMutex(app->lock);
}
