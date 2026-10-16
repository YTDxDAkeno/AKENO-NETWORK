/* PS5 capture capability diagnostic. See capture_diag.h and docs/PS5_DIRECT_CAPTURE_RESEARCH.md. */
#include "capture_diag.h"

#include <string.h>

/*
 * The candidate catalogue. Every symbol name comes from a cited source and is only ever looked
 * up (dlsym), never called. SDK entries are source-verified against the pinned ps5-payload-sdk
 * stubs this repo builds with; the "ext" entries use PS4-origin names from the OpenOrbis SDK and
 * are flagged as such — their presence and signatures on a PS5 are unverified.
 */
static const akeno_capture_candidate k_catalogue[] = {
    { "libSceVideoOut.sprx",
      { "sceVideoOutOpen", "sceVideoOutSysReadCrc64" }, 2, AKENO_CAPROLE_OWN_OUTPUT, 1,
      "pinned ps5-payload-sdk (sce_stubs/libSceVideoOut.c)",
      "Drives the calling process's own display output; ReadCrc returns a frame checksum, not pixels. No interface to read another game's image.",
      0, NULL, "12.20 hardware run 2 (build 12a06a06610c): loaded, 2/2 symbol names present." },

    { "libSceSystemService.sprx",
      { "sceSystemServiceIsGameLiveStreamingOnAir" }, 1, AKENO_CAPROLE_STATUS, 1,
      "pinned ps5-payload-sdk (sce_stubs/libSceSystemService.c)",
      "GameLiveStreaming/SharePlay status, pre-notify and CPU-budget hooks (sceShellCoreUtil*). Orchestration only; hands out no pixels.",
      0, NULL, "12.20 hardware run 2 (build 12a06a06610c): loaded, 1/1 symbol name present." },

    { "libSceRemoteplay.sprx",
      { "sceRemoteplayInitialize", "sceRemoteplayGetConnectionStatus", "sceRemoteplayProhibitStreaming" }, 3, AKENO_CAPROLE_STATUS, 1,
      "pinned ps5-payload-sdk (sce_stubs/libSceRemoteplay.c)",
      "Controls/queries the system Remote Play server; it does not deliver the encoded video to our process.",
      0, NULL, "12.20 hardware run 2 (build 12a06a06610c): loaded, 3/3 symbol names present." },

    { "libSceScreenShot.sprx",
      { "sceScreenShotCapture", "sceScreenShotEnable", "sceScreenShotSetParam" }, 3, AKENO_CAPROLE_SCREENSHOT, 0,
      "OpenOrbis PS4 SDK (PS4 origin; PS5 presence unverified, arguments unverified)",
      "At most a single still frame on request, and historically the caller's own surface. A still is not live SharePlay.",
      0, NULL, "12.20 hardware run 2 (build 12a06a06610c): loaded, 3/3 symbol names present (names only — not called)." },

    { "libSceVideoRecording.sprx",
      { "sceVideoRecordingOpen", "sceVideoRecordingStart", "sceVideoRecordingStop" }, 3, AKENO_CAPROLE_RECORDING, 0,
      "OpenOrbis PS4 SDK (PS4 origin; PS5 presence unverified, arguments unverified)",
      "Writes a clip to storage; reading a saved clip is not a live interactive stream.",
      1, "dlopen did not return on 12.20 hardware run 2 (build 12a06a06610c): loading it hung or terminated the app. Quarantined; not loaded until a safe method exists.",
      "12.20 hardware run 2 (build 12a06a06610c): dlopen BEGAN and never returned (no end log line). Treated as unsafe to load." },

    { "libSceGameLiveStreaming.sprx",
      { "sceGameLiveStreamingEnable", "sceGameLiveStreamingStartBroadcast", "sceGameLiveStreamingStopBroadcast" }, 3, AKENO_CAPROLE_BROADCAST, 0,
      "OpenOrbis PS4 SDK (PS4 origin; PS5 presence unverified, arguments unverified)",
      "The console's own broadcaster (Twitch/YouTube). It needs a linked account and network session; it is not a raw-frame API, and its destination is chosen by the system, not by a parameter.",
      1, "Same load-and-start risk class as libSceVideoRecording (a broadcast system module). dlopen would LoadStartModule it; no no-load symbol-presence method exists within our import gate. Quarantined pending a safe method; see docs/PS5_DIRECT_CAPTURE_RESEARCH.md.",
      NULL },
};

const akeno_capture_candidate *akeno_capture_catalogue(size_t *count) {
    *count = sizeof(k_catalogue) / sizeof(k_catalogue[0]);
    return k_catalogue;
}

akeno_cap_level akeno_capture_level(const akeno_capture_candidate *candidate, const akeno_capture_probe *probe) {
    if (probe && probe->resolved && probe->load_ok) {
        for (size_t i = 0; i < candidate->symbol_count; i++) {
            if (probe->symbol_found[i]) return AKENO_CAPLV_SYMBOL_PRESENT;
        }
    }
    if (probe && probe->file_present) return AKENO_CAPLV_RUNTIME_DETECTED;
    if (candidate->source && candidate->source[0]) return AKENO_CAPLV_SOURCE_VERIFIED;
    return AKENO_CAPLV_UNKNOWN;
}

const char *akeno_cap_level_name(akeno_cap_level level) {
    switch (level) {
        case AKENO_CAPLV_UNSUPPORTED: return "unsupported";
        case AKENO_CAPLV_SOURCE_VERIFIED: return "source-verified";
        case AKENO_CAPLV_RUNTIME_DETECTED: return "runtime-detected";
        case AKENO_CAPLV_SYMBOL_PRESENT: return "symbol-present";
        case AKENO_CAPLV_HARDWARE_TESTED: return "hardware-tested";
        case AKENO_CAPLV_UNKNOWN:
        default: return "unknown";
    }
}

const char *akeno_cap_role_name(akeno_cap_role role) {
    switch (role) {
        case AKENO_CAPROLE_STATUS: return "status-only";
        case AKENO_CAPROLE_OWN_OUTPUT: return "own-process-output";
        case AKENO_CAPROLE_CHECKSUM: return "frame-checksum";
        case AKENO_CAPROLE_SCREENSHOT: return "screenshot";
        case AKENO_CAPROLE_RECORDING: return "recorded-clip";
        case AKENO_CAPROLE_BROADCAST: return "native-broadcast";
        case AKENO_CAPROLE_LIVE_FRAMES: return "live-game-frames";
        case AKENO_CAPROLE_ENCODER: return "hardware-encoder";
        default: return "unknown";
    }
}

/* No current candidate hands our process continuous frames of another running game. */
static int gives_live_game_frames(akeno_cap_role role) {
    (void)role;
    return 0;
}

static void write_capability(akeno_buf *b, const char *key, int supported, const char *reason, int comma) {
    akeno_buf_append_str(b, "\"");
    akeno_buf_append_str(b, key);
    akeno_buf_append_str(b, "\":{\"supported\":");
    akeno_buf_append_str(b, supported ? "true" : "false");
    akeno_buf_append_str(b, ",\"reason\":");
    akeno_buf_append_json_string(b, reason);
    akeno_buf_append_str(b, comma ? "}," : "}");
}

int akeno_capture_report_json(akeno_buf *b, const akeno_capture_sysinfo *sys, const akeno_capture_network *net,
                              const akeno_capture_probe *probes, size_t probe_count, const char *now_iso) {
    size_t count = 0;
    const akeno_capture_candidate *cat = akeno_capture_catalogue(&count);
    int rc = 0;

    rc |= akeno_buf_append_str(b, "{\"experimental\":true,\"kind\":\"ps5-capture-diagnostic\",\"generatedAt\":");
    rc |= akeno_buf_append_json_string(b, now_iso ? now_iso : "");

    rc |= akeno_buf_append_str(b, ",\"system\":{\"platform\":");
    rc |= akeno_buf_append_json_string(b, sys->platform ? sys->platform : "");
    rc |= akeno_buf_append_str(b, ",\"firmware\":");
    rc |= akeno_buf_append_json_string(b, sys->firmware ? sys->firmware : "");
    rc |= akeno_buf_printf(b, ",\"firmwareRaw\":%u,\"appVersion\":", sys->firmware_raw);
    rc |= akeno_buf_append_json_string(b, sys->app_version ? sys->app_version : "");
    rc |= akeno_buf_append_str(b, ",\"buildId\":");
    rc |= akeno_buf_append_json_string(b, sys->build_id ? sys->build_id : "");
    rc |= akeno_buf_append_str(b, ",\"sdkCommit\":");
    rc |= akeno_buf_append_json_string(b, sys->sdk_commit ? sys->sdk_commit : "");
    rc |= akeno_buf_append_str(b, ",\"libraryDir\":");
    rc |= akeno_buf_append_json_string(b, sys->library_dir ? sys->library_dir : "");
    rc |= akeno_buf_printf(b, ",\"libraryDirReadable\":%s,\"processInfoAvailable\":%s,\"resolveRan\":%s}",
                           sys->library_dir_readable ? "true" : "false", sys->process_info_available ? "true" : "false",
                           sys->resolve_ran ? "true" : "false");

    /* Honest, fixed capability verdict: not derived from an optimistic reading of symbol presence. */
    rc |= akeno_buf_append_str(b, ",\"capabilities\":{");
    write_capability(b, "liveFramebufferCapture", 0,
                     "No verified interface lets a payload read the video of another running game, and AKENO does not read another process's memory.", 1);
    write_capability(b, "screenshot", 0,
                     "At most a still of the caller's own surface, unverified on PS5; a still is not live SharePlay.", 1);
    write_capability(b, "recordedClipAccess", 0,
                     "Reading a saved clip is not a live stream.", 1);
    write_capability(b, "nativeBroadcastReuse", 0,
                     "The console broadcaster needs a linked account and network session and picks its own destination; see docs/PS5_DIRECT_CAPTURE_RESEARCH.md track B.", 1);
    write_capability(b, "hardwareEncoder", 0,
                     "No encoder interface is declared in the pinned SDK and no verified library/signature is known.", 0);
    rc |= akeno_buf_append_str(b, "}");

    rc |= akeno_buf_append_str(b, ",\"libraries\":[");
    for (size_t i = 0; i < count; i++) {
        const akeno_capture_candidate *c = &cat[i];
        const akeno_capture_probe *p = (probes && i < probe_count) ? &probes[i] : NULL;
        akeno_cap_level level = akeno_capture_level(c, p);
        if (i) rc |= akeno_buf_append_str(b, ",");
        rc |= akeno_buf_append_str(b, "{\"library\":");
        rc |= akeno_buf_append_json_string(b, c->library);
        rc |= akeno_buf_append_str(b, ",\"role\":");
        rc |= akeno_buf_append_json_string(b, akeno_cap_role_name(c->role));
        rc |= akeno_buf_printf(b, ",\"inPinnedSdk\":%s,\"source\":", c->in_pinned_sdk ? "true" : "false");
        rc |= akeno_buf_append_json_string(b, c->source);
        rc |= akeno_buf_append_str(b, ",\"note\":");
        rc |= akeno_buf_append_json_string(b, c->note);
        rc |= akeno_buf_printf(b, ",\"loadUnsafe\":%s", c->load_unsafe ? "true" : "false");
        rc |= akeno_buf_append_str(b, ",\"unsafeReason\":");
        rc |= akeno_buf_append_json_string(b, c->unsafe_reason ? c->unsafe_reason : "");
        rc |= akeno_buf_append_str(b, ",\"hardwareNote\":");
        rc |= akeno_buf_append_json_string(b, c->hardware_note ? c->hardware_note : "");
        rc |= akeno_buf_printf(b, ",\"filePresent\":%s", p && p->file_present ? "true" : (p ? "false" : "null"));
        rc |= akeno_buf_printf(b, ",\"resolved\":%s", p && p->resolved ? "true" : "false");
        rc |= akeno_buf_printf(b, ",\"blocked\":%s", p && p->blocked ? "true" : "false");
        if (p && p->resolved) rc |= akeno_buf_printf(b, ",\"loadOk\":%s", p->load_ok ? "true" : "false");
        else rc |= akeno_buf_append_str(b, ",\"loadOk\":null");
        rc |= akeno_buf_append_str(b, ",\"level\":");
        rc |= akeno_buf_append_json_string(b, akeno_cap_level_name(level));
        rc |= akeno_buf_printf(b, ",\"givesLiveGameFrames\":%s,\"symbols\":[", gives_live_game_frames(c->role) ? "true" : "false");
        for (size_t s = 0; s < c->symbol_count; s++) {
            if (s) rc |= akeno_buf_append_str(b, ",");
            rc |= akeno_buf_append_str(b, "{\"name\":");
            rc |= akeno_buf_append_json_string(b, c->symbols[s]);
            if (p && p->resolved && p->load_ok) rc |= akeno_buf_printf(b, ",\"found\":%s}", p->symbol_found[s] ? "true" : "false");
            else rc |= akeno_buf_append_str(b, ",\"found\":null}");
        }
        rc |= akeno_buf_append_str(b, "]");
        if (p && p->error[0]) {
            rc |= akeno_buf_append_str(b, ",\"error\":");
            rc |= akeno_buf_append_json_string(b, p->error);
        }
        rc |= akeno_buf_append_str(b, "}");
    }
    rc |= akeno_buf_append_str(b, "]");

    rc |= akeno_buf_append_str(b, ",\"network\":");
    if (net && net->have) {
        rc |= akeno_buf_printf(b, "{\"serverConfigured\":%s,\"serverReachable\":%s,\"serverDetail\":",
                               net->server_configured ? "true" : "false", net->server_reachable ? "true" : "false");
        rc |= akeno_buf_append_json_string(b, net->server_detail);
        rc |= akeno_buf_printf(b, ",\"dnsReadable\":%s,\"dnsSummary\":", net->dns_readable ? "true" : "false");
        rc |= akeno_buf_append_json_string(b, net->dns_summary);
        rc |= akeno_buf_append_str(b, "}");
    } else {
        rc |= akeno_buf_append_str(b, "null");
    }

    rc |= akeno_buf_append_str(b,
                               ",\"notes\":["
                               "\"Read-only diagnostic: no capture, encoder or broadcast function is called.\","
                               "\"No console setting, DNS or account is changed; PSN and Twitch are never contacted.\","
                               "\"dlsym confirms a symbol NAME is present (the name resolves to an address). It does NOT verify the function's arguments, signature, or that calling it is safe or does anything useful.\","
                               "\"Libraries marked loadUnsafe are quarantined and never loaded; only their file presence is reported.\""
                               "]}");
    return rc ? -1 : 0;
}
