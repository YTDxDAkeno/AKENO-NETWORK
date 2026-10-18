#include "update_view.h"

#include <stdio.h>
#include <string.h>

void akn_upd_ago(long long then, long long now, char *out, size_t out_len) {
    if (then <= 0) {
        snprintf(out, out_len, "never");
        return;
    }
    long long d = now > then ? now - then : 0;
    if (d < 60) snprintf(out, out_len, "just now");
    else if (d < 3600) snprintf(out, out_len, "%lld minute%s ago", d / 60, d / 60 == 1 ? "" : "s");
    else if (d < 86400) snprintf(out, out_len, "%lld hour%s ago", d / 3600, d / 3600 == 1 ? "" : "s");
    else snprintf(out, out_len, "%lld day%s ago", d / 86400, d / 86400 == 1 ? "" : "s");
}

double akn_upd_progress(const akn_upd_view *v) {
    if (v->phase != AKN_UPD_DOWNLOADING) return -1;
    if (v->total <= 0) return 0;
    double p = (double)v->done / (double)v->total;
    return p < 0 ? 0 : p > 1 ? 1 : p;
}

void akn_upd_progress_text(const akn_upd_view *v, char *out, size_t out_len) {
    char done[32], total[32], left[32], speed[32];
    switch (v->phase) {
    case AKN_UPD_DOWNLOADING:
        akn_update_format_size(v->done, done, sizeof(done));
        akn_update_format_size(v->total > 0 ? v->total : v->size, total, sizeof(total));
        akn_update_format_size((v->total > 0 ? v->total : v->size) - v->done, left, sizeof(left));
        if (v->bytes_per_s > 0) {
            akn_update_format_size((long long)v->bytes_per_s, speed, sizeof(speed));
            snprintf(out, out_len, "Downloading %s of %s \xC2\xB7 %s/s \xC2\xB7 %s left \xC2\xB7 Square: cancel", done, total, speed, left);
        } else {
            snprintf(out, out_len, "Downloading %s of %s \xC2\xB7 %s left \xC2\xB7 Square: cancel", done, total, left);
        }
        break;
    case AKN_UPD_VERIFYING: snprintf(out, out_len, "Checking the download against the signed checksum..."); break;
    case AKN_UPD_UNPACKING: snprintf(out, out_len, "Unpacking and checking every file..."); break;
    case AKN_UPD_INSTALLING: snprintf(out, out_len, "Installing next to the current version..."); break;
    default: out[0] = '\0'; break;
    }
}

static int busy(const akn_upd_view *v) {
    return v->checking || (v->phase >= AKN_UPD_DOWNLOADING && v->phase <= AKN_UPD_INSTALLING);
}

int akn_upd_row_enabled(const akn_upd_view *v, akn_upd_row row) {
    switch (row) {
    case AKN_UPD_ROW_CHANNEL:
    case AKN_UPD_ROW_AUTO: return !busy(v);
    case AKN_UPD_ROW_CHECK: return !busy(v) && v->phase != AKN_UPD_INSTALLED;
    case AKN_UPD_ROW_LATEST:
    case AKN_UPD_ROW_CHANGELOG: return v->checked && v->version[0];
    case AKN_UPD_ROW_INSTALL:
        return (v->phase >= AKN_UPD_DOWNLOADING && v->phase <= AKN_UPD_INSTALLING) /* Square cancels */ ||
               (!busy(v) && v->checked && v->result == AKN_CHECK_AVAILABLE && v->phase != AKN_UPD_INSTALLED);
    case AKN_UPD_ROW_RESTART: return v->phase == AKN_UPD_INSTALLED;
    default: return 0;
    }
}

void akn_upd_row_text(const akn_upd_view *v, akn_upd_row row, char *title, size_t title_len, char *sub, size_t sub_len) {
    char a[64], b[64];
    title[0] = sub[0] = '\0';
    switch (row) {
    case AKN_UPD_ROW_CURRENT:
        snprintf(title, title_len, "Current version");
        if (v->boot == AKN_BOOT_TRIAL)
            snprintf(sub, sub_len, "%s \xC2\xB7 newly installed, start %d of %d \xC2\xB7 build %s", v->own_version, v->boot_attempt,
                     AKN_UPDATE_MAX_ATTEMPTS, v->build);
        else if (v->boot == AKN_BOOT_ROLLED_BACK)
            snprintf(sub, sub_len, "%s \xC2\xB7 %s did not start, so AKENO went back to this version", v->own_version, v->failed_version);
        else if (v->confirmed_now)
            snprintf(sub, sub_len, "%s \xC2\xB7 update finished \xC2\xB7 build %s", v->own_version, v->build);
        else snprintf(sub, sub_len, "%s \xC2\xB7 build %s", v->own_version, v->build);
        break;
    case AKN_UPD_ROW_CHANNEL:
        snprintf(title, title_len, "Update channel: %s", v->prefs.channel == AKN_CHANNEL_BETA ? "Beta" : "Stable");
        snprintf(sub, sub_len, "%s", v->prefs.channel == AKN_CHANNEL_BETA ? "Early versions (pre-releases) and stable ones. Cross: switch to Stable"
                                                                        : "Released versions only. Cross: switch to Beta");
        break;
    case AKN_UPD_ROW_CHECK:
        snprintf(title, title_len, "Check for updates");
        akn_upd_ago(v->prefs.last_check, v->now_unix, a, sizeof(a));
        if (v->checking) snprintf(sub, sub_len, "Checking GitHub...");
        else if (!v->checked) snprintf(sub, sub_len, "Last checked: %s", a);
        else if (v->result == AKN_CHECK_UP_TO_DATE) snprintf(sub, sub_len, "AKENO NETWORK is up to date (checked %s).", a);
        else if (v->result == AKN_CHECK_AVAILABLE) snprintf(sub, sub_len, "Version %s is available (checked %s).", v->version, a);
        else snprintf(sub, sub_len, "%s", v->why);
        break;
    case AKN_UPD_ROW_AUTO:
        snprintf(title, title_len, "Automatic update checks");
        snprintf(sub, sub_len, "At start and every 6 hours while AKENO runs. It only asks; nothing installs without you.");
        break;
    case AKN_UPD_ROW_LATEST:
        snprintf(title, title_len, "Latest release");
        if (!v->checked || !v->version[0]) snprintf(sub, sub_len, "%s", v->checking ? "Checking..." : "Not checked yet.");
        else {
            akn_update_format_size(v->size, a, sizeof(a));
            snprintf(sub, sub_len, "%s%s%s%s%s%s", v->version, v->prerelease ? " (beta)" : "", v->size > 0 ? " \xC2\xB7 " : "",
                     v->size > 0 ? a : "", v->published[0] ? " \xC2\xB7 " : "", v->published);
            if (v->previously_failed) {
                size_t n = strlen(sub);
                snprintf(sub + n, sub_len - n, " \xC2\xB7 did not start here before");
            }
        }
        break;
    case AKN_UPD_ROW_CHANGELOG:
        snprintf(title, title_len, "View changelog");
        snprintf(sub, sub_len, "%s", v->checked && v->version[0] ? "What changed, as written on the release (right)." : "Check for updates first.");
        break;
    case AKN_UPD_ROW_INSTALL:
        snprintf(title, title_len, "Download and install");
        if (v->phase >= AKN_UPD_DOWNLOADING && v->phase <= AKN_UPD_INSTALLING) akn_upd_progress_text(v, sub, sub_len);
        else if (v->phase == AKN_UPD_INSTALLED) snprintf(sub, sub_len, "Version %s is installed.", v->installed_version);
        else if (v->phase == AKN_UPD_FAILED) snprintf(sub, sub_len, "%s%s", akn_upd_row_enabled(v, row) ? "Cross: try again \xC2\xB7 " : "", v->error);
        else if (v->checked && v->result == AKN_CHECK_AVAILABLE) {
            akn_update_format_size(v->size, a, sizeof(a));
            snprintf(sub, sub_len, "Version %s, %s. Your data and sign-in stay as they are.", v->version, a);
        } else if (v->checked && (v->result == AKN_CHECK_INCOMPATIBLE || v->result == AKN_CHECK_UNVERIFIED || v->result == AKN_CHECK_REJECTED))
            snprintf(sub, sub_len, "Not available: %s", v->why);
        else snprintf(sub, sub_len, "%s", v->has_root ? "No update to install." : "Start AKENO from the Homebrew Launcher to install updates.");
        break;
    case AKN_UPD_ROW_RESTART:
        snprintf(title, title_len, "Restart to apply");
        if (v->phase == AKN_UPD_INSTALLED)
            snprintf(sub, sub_len, "Cross: close AKENO. Start it again from the Homebrew Launcher to use %s.", v->installed_version);
        else snprintf(sub, sub_len, "After an update is installed.");
        break;
    case AKN_UPD_ROW_HISTORY:
        snprintf(title, title_len, "Update history");
        snprintf(b, sizeof(b), "%.60s", v->history_last);
        snprintf(sub, sub_len, "%s", v->history_last[0] ? b : "No updates installed yet.");
        break;
    case AKN_UPD_ROW_COUNT: break;
    }
}
