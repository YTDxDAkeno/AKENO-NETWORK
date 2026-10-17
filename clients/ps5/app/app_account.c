/*
 * AKENO Account Bridge, console app glue (see account_bridge.h, platform_account.h). It reads the
 * selected local user, runs the optional read-only account check when the user asks, and links
 * the local user to the paired AKENO account through the existing device API. It never writes to
 * the console, never calls a Remote Play function, and never stores the account ID value.
 */
#if defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#endif
#include <string.h>

#include "account_bridge.h"
#include "app_internal.h"
#include "transport.h"

/* Logs each read-only step with the exact operation, flushed so a crash leaves the last line. */
static void bridge_step(void *ctx, const char *event, const char *detail) {
    akeno_app *app = ctx;
    app_log(app, AKENO_LOG_DEBUG, event, "%s", detail);
    app_log_sync(app);
}

void app_bridge_init(akeno_app *app) {
    pthread_mutex_lock(&app->lock);
    app->bridge_have_user = 0;
    app->bridge_checked = 0;
    app->bridge_link_state[0] = '\0';
    pthread_mutex_unlock(&app->lock);
    app_bridge_refresh_user(app);
}

void app_bridge_refresh_user(akeno_app *app) {
    akeno_console_user user;
    char err[160];
    int rc = akeno_platform_console_user(&user, err, sizeof(err), bridge_step, app);
    pthread_mutex_lock(&app->lock);
    app->bridge_have_user = rc == 0;
    if (rc == 0) app->bridge_user = user;
    pthread_mutex_unlock(&app->lock);
}

/*
 * Opt-in, read-only account check. Runs only when the user starts it. It loads the registry and
 * Remote Play libraries, looks up function names (presence), and, when the read functions exist,
 * reads this user's account-ID presence and the Remote Play settings. Nothing is written.
 */
void app_bridge_run_check(akeno_app *app) {
    app_bridge_refresh_user(app);
    pthread_mutex_lock(&app->lock);
    int have = app->bridge_have_user;
    akeno_console_user user = app->bridge_user;
    pthread_mutex_unlock(&app->lock);

    app_log(app, AKENO_LOG_INFO, "bridge.check.start", "Read-only account check started by the user.");
    app_log_sync(app);
    akeno_account_presence presence;
    akeno_platform_account_presence(&presence, bridge_step, app);
    akeno_account_read read;
    memset(&read, 0, sizeof(read));
    if (have && presence.regmgr_get_int && presence.regmgr_get_bin)
        akeno_platform_account_read(user.user_id, &read, bridge_step, app);

    pthread_mutex_lock(&app->lock);
    app->bridge_presence = presence;
    app->bridge_read = read;
    app->bridge_checked = 1;
    pthread_mutex_unlock(&app->lock);
    app_log(app, AKENO_LOG_INFO, "bridge.check.done", "Read-only account check finished.");
    app_log_sync(app);
}

/* Sends the link to the server. Caller must not hold app->lock. */
static akeno_status bridge_put(akeno_app *app, const akeno_console_user *user, const char *account_state) {
    akeno_buf body;
    akeno_buf_init(&body, 256);
    if (akeno_bridge_link_body(user, account_state, &body) != 0) {
        akeno_buf_free(&body);
        return AKENO_ERR_NO_MEMORY;
    }
    akeno_response response;
    akeno_status status = akeno_request(app->ui_client, "PUT", "/devices/current/console-user", body.data, &response);
    akeno_buf_free(&body);
    if (status == AKENO_OK && response.http_status != 200) status = AKENO_ERR_HTTP;
    if (status == AKENO_OK) {
        app_log(app, AKENO_LOG_INFO, "bridge.linked", "Linked the local console user to this AKENO account.");
    } else {
        app_log(app, AKENO_LOG_WARN, "bridge.link_failed", "Could not link the local console user (status %d).", response.http_status);
    }
    akeno_response_free(&response);
    return status;
}

akeno_status app_bridge_link(akeno_app *app, int with_check) {
    app_bridge_refresh_user(app);
    pthread_mutex_lock(&app->lock);
    int have = app->bridge_have_user;
    akeno_console_user user = app->bridge_user;
    int paired = app->ui_client && akeno_client_is_paired(app->ui_client);
    snprintf(app->bridge_link_state, sizeof(app->bridge_link_state), "%s", "linking");
    app->bridge_link_error[0] = '\0';
    pthread_mutex_unlock(&app->lock);

    const char *fail = NULL;
    if (!have) fail = "No local user is selected on this console.";
    else if (!paired) fail = "Pair this console with your AKENO account first.";

    const char *account_state = NULL;
    if (!fail && with_check) {
        app_bridge_run_check(app);
        pthread_mutex_lock(&app->lock);
        account_state = akeno_bridge_account_state(app->bridge_checked ? &app->bridge_read : NULL);
        pthread_mutex_unlock(&app->lock);
    }

    akeno_status status = fail ? AKENO_ERR_ARGUMENT : bridge_put(app, &user, account_state);
    pthread_mutex_lock(&app->lock);
    if (fail) {
        snprintf(app->bridge_link_state, sizeof(app->bridge_link_state), "%s", "error");
        snprintf(app->bridge_link_error, sizeof(app->bridge_link_error), "%s", fail);
    } else if (status == AKENO_OK) {
        snprintf(app->bridge_link_state, sizeof(app->bridge_link_state), "%s", "linked");
        app->bridge_linked_at = akeno_now_ms();
    } else {
        snprintf(app->bridge_link_state, sizeof(app->bridge_link_state), "%s", "error");
        snprintf(app->bridge_link_error, sizeof(app->bridge_link_error), "The AKENO server did not accept the link.");
    }
    pthread_mutex_unlock(&app->lock);
    return status;
}

void app_bridge_unlink(akeno_app *app) {
    akeno_response response;
    akeno_status status = app->ui_client ? akeno_request(app->ui_client, "DELETE", "/devices/current/console-user", NULL, &response)
                                         : AKENO_ERR_ARGUMENT;
    pthread_mutex_lock(&app->lock);
    app->bridge_link_state[0] = '\0';
    app->bridge_linked_at = 0;
    pthread_mutex_unlock(&app->lock);
    if (status == AKENO_OK) {
        akeno_response_free(&response);
        app_log(app, AKENO_LOG_INFO, "bridge.unlinked", "Removed the local console user link.");
    }
}

void app_bridge_json(akeno_app *app, akeno_buf *b) {
    pthread_mutex_lock(&app->lock);
    akeno_buf_append_str(b, "{\"haveUser\":");
    akeno_buf_append_str(b, app->bridge_have_user ? "true" : "false");
    if (app->bridge_have_user) {
        char name[AKENO_BRIDGE_NAME_MAX + 1];
        akeno_bridge_clean_name(app->bridge_user.name, name, sizeof(name));
        akeno_buf_printf(b, ",\"localUserId\":%d,\"userName\":", app->bridge_user.user_id);
        akeno_buf_append_json_string(b, name);
    }
    akeno_buf_append_str(b, ",\"linkState\":");
    akeno_buf_append_json_string(b, app->bridge_link_state);
    akeno_buf_append_str(b, ",\"linkError\":");
    akeno_buf_append_json_string(b, app->bridge_link_error);
    akeno_buf_append_str(b, ",\"checked\":");
    akeno_buf_append_str(b, app->bridge_checked ? "true" : "false");
    akeno_buf_append_str(b, ",");
    akeno_bridge_results_json(app->bridge_checked ? &app->bridge_presence : NULL,
                              app->bridge_checked && app->bridge_read.done ? &app->bridge_read : NULL, b);
    akeno_buf_append_str(b, "}");
    pthread_mutex_unlock(&app->lock);
}
