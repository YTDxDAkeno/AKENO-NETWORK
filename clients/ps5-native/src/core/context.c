#include "context.h"

#include <stdio.h>
#include <string.h>

#include "game_detect.h"

void akn_context_decode(akn_context_raw *raw) {
    raw->own_app_id = 0;
    raw->own_title_id[0] = '\0';
    if (!raw->have_appinfo_fn || raw->appinfo_rc != 0) return;
    akeno_proc_info info;
    memset(&info, 0, sizeof(info));
    akeno_appinfo_parse(raw->appinfo_raw, sizeof(raw->appinfo_raw), &info);
    raw->own_app_id = info.app_id;
    const char *tid = info.title_id[0] ? info.title_id : info.title_id_alt;
    snprintf(raw->own_title_id, sizeof(raw->own_title_id), "%s", tid);
}

akn_context_kind akn_context_classify(const akn_context_raw *raw) {
    if (!raw->have_appinfo_fn || raw->appinfo_rc != 0 || !raw->have_bigapp_fn) return AKN_CTX_UNKNOWN;
    if (raw->bigapp_app_id <= 0) return AKN_CTX_BACKGROUND; /* no foreground application at all */
    if (raw->own_app_id == 0) return AKN_CTX_BACKGROUND;    /* not an application process */
    return raw->own_app_id == (uint32_t)raw->bigapp_app_id ? AKN_CTX_FOREGROUND : AKN_CTX_BACKGROUND;
}

const char *akn_context_name(akn_context_kind kind) {
    switch (kind) {
    case AKN_CTX_FOREGROUND: return "foreground application";
    case AKN_CTX_BACKGROUND: return "background process";
    case AKN_CTX_DESKTOP: return "desktop build";
    case AKN_CTX_UNKNOWN: break;
    }
    return "unknown";
}

int akn_context_allows_devices(const akn_context_raw *raw, akn_context_kind kind) {
    return kind == AKN_CTX_FOREGROUND || kind == AKN_CTX_DESKTOP || raw->launcher_arg;
}

void akn_context_describe(const akn_context_raw *raw, akn_context_kind kind, char *buf, size_t len) {
    if (kind == AKN_CTX_DESKTOP) {
        snprintf(buf, len, "desktop build (no console process checks)");
        return;
    }
    char appinfo[48], bigapp[40];
    if (!raw->have_appinfo_fn) snprintf(appinfo, sizeof(appinfo), "unavailable");
    else if (raw->appinfo_rc != 0) snprintf(appinfo, sizeof(appinfo), "error 0x%08x", (unsigned)raw->appinfo_rc);
    else snprintf(appinfo, sizeof(appinfo), "0x%08x %s", raw->own_app_id, raw->own_title_id[0] ? raw->own_title_id : "(no title ID)");
    if (!raw->have_bigapp_fn) snprintf(bigapp, sizeof(bigapp), "unavailable");
    else snprintf(bigapp, sizeof(bigapp), "0x%08x", (unsigned)raw->bigapp_app_id);
    const char *note = "";
    if (raw->launcher_arg && kind != AKN_CTX_FOREGROUND) note = "; started by the launcher but the check disagrees";
    snprintf(buf, len, "%s (own app %s, foreground app %s%s%s)", akn_context_name(kind), appinfo, bigapp,
             raw->launcher_arg ? ", --launcher" : "", note);
}
