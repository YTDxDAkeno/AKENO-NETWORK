#include "akeno/caps.h"

#include <ctype.h>
#include <string.h>

static const char *const FEATURE_NAMES[AKENO_FEATURE_COUNT] = {
    "device-pairing",        "rest-api",        "realtime",           "credential-storage",
    "native-friends-list",   "native-presence", "native-messages",    "native-party",
    "native-party-voice",    "native-game-invites", "system-notifications",
};

const char *akeno_feature_name(akeno_feature feature) {
    return feature >= 0 && feature < AKENO_FEATURE_COUNT ? FEATURE_NAMES[feature] : "unknown";
}

const char *akeno_cap_state_name(akeno_cap_state state) {
    switch (state) {
    case AKENO_CAP_ENABLED: return "enabled";
    case AKENO_CAP_DISABLED: return "disabled";
    default: return "unsupported";
    }
}

/*
 * Firmware versions the project tracks. "hardware_tested" stays 0 until a physical-console test
 * is documented in docs/FIRMWARE_COMPATIBILITY.md. The client's network features do not depend
 * on firmware internals, so these entries never enable anything by themselves.
 */
static const struct {
    int major, minor;
    const char *note;
} FIRMWARE_TABLE[] = {
    { 12, 20, "Priority target. A toolchain diagnostic (notification) ran on hardware; the client is not yet tested on hardware." },
    { 13, 60, "Priority target. Not yet tested on hardware." },
};

int akeno_firmware_parse(const char *text, int *major, int *minor) {
    if (!text) return -1;
    size_t len = strlen(text);
    if (len < 4 || len > 5) return -1;
    const char *dot = strchr(text, '.');
    if (!dot || dot == text || dot - text > 2 || strlen(dot + 1) != 2) return -1;
    for (const char *p = text; *p; p++)
        if (p != dot && !isdigit((unsigned char)*p)) return -1;
    *major = 0;
    for (const char *p = text; p < dot; p++) *major = *major * 10 + (*p - '0');
    *minor = (dot[1] - '0') * 10 + (dot[2] - '0');
    return *major >= 1 ? 0 : -1;
}

void akeno_firmware_describe(const char *text, akeno_firmware_info *out) {
    memset(out, 0, sizeof(*out));
    out->note = text ? "Unrecognized firmware version format." : "Firmware version unknown.";
    if (akeno_firmware_parse(text, &out->major, &out->minor) != 0) return;
    out->note = "Not in the compatibility table. Firmware-specific features stay disabled.";
    for (size_t i = 0; i < sizeof(FIRMWARE_TABLE) / sizeof(FIRMWARE_TABLE[0]); i++) {
        if (FIRMWARE_TABLE[i].major == out->major && FIRMWARE_TABLE[i].minor == out->minor) {
            out->known = 1;
            out->hardware_tested = 0;
            out->note = FIRMWARE_TABLE[i].note;
        }
    }
}

static void set(akeno_capability_report *r, akeno_feature f, akeno_cap_state state, const char *reason) {
    r->features[f].state = state;
    r->features[f].reason = reason;
}

void akeno_caps_evaluate(const akeno_platform_facts *facts, akeno_capability_report *out) {
    memset(out, 0, sizeof(*out));
    akeno_firmware_describe(facts->platform == AKENO_PLATFORM_PS5 ? facts->firmware : NULL, &out->firmware);
    if (facts->platform == AKENO_PLATFORM_HOST) out->firmware.note = "Not applicable on a host computer.";

    int network = facts->has_sockets && facts->has_tls;
    const char *no_network = !facts->has_sockets ? "BSD sockets are not available in this environment."
                                                 : "No verifying TLS backend is available; plain connections are refused.";
    set(out, AKENO_FEATURE_REST_API, network ? AKENO_CAP_ENABLED : AKENO_CAP_DISABLED,
        network ? "HTTPS with certificate and host name verification." : no_network);
    set(out, AKENO_FEATURE_REALTIME, network ? AKENO_CAP_ENABLED : AKENO_CAP_DISABLED,
        network ? "WSS with bounded reconnects." : no_network);
    set(out, AKENO_FEATURE_CREDENTIAL_STORAGE, facts->has_storage ? AKENO_CAP_ENABLED : AKENO_CAP_DISABLED,
        facts->has_storage ? "Device token stored in a private file (mode 0600)." : "The credential directory is not writable.");
    int pairing = network && facts->has_storage;
    set(out, AKENO_FEATURE_DEVICE_PAIRING, pairing ? AKENO_CAP_ENABLED : AKENO_CAP_DISABLED,
        pairing ? "Code-based pairing approved on the website." : "Requires network access and credential storage.");

    static const char *const native =
        "No documented interface lets homebrew provide this through the system UI without modifying "
        "system software. See docs/PS5_GAMEBASE_RESEARCH.md.";
    set(out, AKENO_FEATURE_NATIVE_FRIENDS_LIST, AKENO_CAP_UNSUPPORTED, native);
    set(out, AKENO_FEATURE_NATIVE_PRESENCE, AKENO_CAP_UNSUPPORTED, native);
    set(out, AKENO_FEATURE_NATIVE_MESSAGES, AKENO_CAP_UNSUPPORTED, native);
    set(out, AKENO_FEATURE_NATIVE_PARTY, AKENO_CAP_UNSUPPORTED, native);
    set(out, AKENO_FEATURE_NATIVE_PARTY_VOICE, AKENO_CAP_UNSUPPORTED, native);
    set(out, AKENO_FEATURE_NATIVE_GAME_INVITES, AKENO_CAP_UNSUPPORTED, native);
    if (facts->platform != AKENO_PLATFORM_PS5) {
        set(out, AKENO_FEATURE_SYSTEM_NOTIFICATIONS, AKENO_CAP_UNSUPPORTED, "Not applicable on a host computer.");
    } else if (facts->has_notifications) {
        set(out, AKENO_FEATURE_SYSTEM_NOTIFICATIONS, AKENO_CAP_ENABLED,
            "sceKernelSendNotificationRequest as in the payload SDK sample; the call was confirmed on a PS5 with "
            "firmware 12.20. Notifications show plain text only.");
    } else {
        set(out, AKENO_FEATURE_SYSTEM_NOTIFICATIONS, AKENO_CAP_DISABLED, "This build has no reviewed notification call.");
    }
}
