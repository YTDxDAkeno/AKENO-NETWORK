/*
 * Module B: system libraries and exported symbols. Each library is checked for file presence and,
 * unless quarantined, dlopen'd once; each symbol is looked up with dlsym. Addresses are compared
 * with NULL and NEVER called. A resolved symbol proves only that the NAME exists in this firmware -
 * not its argument types, nor that calling it is safe. Quarantined libraries (load_unsafe) are
 * never dlopen'd: a broadcast/recording module hung dlopen on 12.20 in earlier AKENO research
 * (feature/akeno-ps5-direct-capture, hardware run 2).
 */
#include "ex.h"

#include <stdio.h>
#include <string.h>

/* The two audio-focus/attach names Sony's libSceAudioOut exports (names only, no declaration). */
#define AUDIO_FOCUS_SOURCE "libSceAudioOut stub (firmware exports); PS5_AUDIO_RESEARCH.md"

static const ex_lib_entry k_catalogue[] = {
    { "libSceAudioOut.sprx", "PCM audio output (AKENO's own port)",
      "ps5-payload-dev/SDL src/audio/ps5; AKENO audio labs (12.20)", 0, 1,
      { "sceAudioOutInit", "sceAudioOutOpen", "sceAudioOutOutput", "sceAudioOutClose",
        "sceAudioOutGetPortState", "sceAudioOutGetFocusEnablePid", "sceAudioOutAttachToApplicationByPid",
        "sceAudioOutDetachFromApplicationByPid", "sceAudioOutChangeAppModuleState" }, 9,
      { "int32(void)", "int32(i32 user,i32 type,i32 idx,u32 len,u32 freq,u32 param)",
        "int32(i32 handle,const void*)", "int32(i32 handle)",
        "int32(i32 handle,void* state) [args agreed; struct size unverified]",
        NULL, NULL, NULL, NULL },
      { 1, 1, 1, 1, 0, 0, 0, 0, 0 } },

    { "libSceAudioIn.sprx", "microphone capture",
      "AKENO Native 1.1 (12.20, foreground); PS5-Audio-Passthrough-Research", 0, 1,
      { "sceAudioInOpen", "sceAudioInInput", "sceAudioInClose", "sceAudioInGetSilentState" }, 4,
      { "int32(i32 user,u32 type,u32 idx,u32 grain,u32 freq,u32 fmt)", "int32(i32 handle,void*)",
        "int32(i32 handle)", NULL },
      { 1, 1, 1, 0 } },

    { "libSceUserService.sprx", "logged-in / foreground user",
      "AKENO daemon and Native (12.20)", 0, 1,
      { "sceUserServiceInitialize", "sceUserServiceGetForegroundUser", "sceUserServiceGetLoginUserIdList" }, 3,
      { "int(void*)", "int(u32* user)", "int(void* list)" },
      { 1, 1, 0 } },

    { "libSceSystemService.sprx", "foreground app id, app status",
      "AKENO daemon and Native (12.20); etaHEN", 0, 1,
      { "sceSystemServiceGetAppIdOfRunningBigApp", "sceSystemServiceGetAppTitleId",
        "sceSystemServiceGetStatus", "sceSystemServiceGetAppStatus" }, 4,
      { "int(void)", "int(int app_id,char* title_id)", "int(void* status)", NULL },
      { 1, 0, 0, 0 } },

    { "libScePad.sprx", "controller state",
      "ps5-payload-dev/SDL; AKENO daemon opt-in (12.20: scePadOpen 0x809B0081 in background)", 0, 1,
      { "scePadInit", "scePadGetHandle", "scePadOpen", "scePadReadState", "scePadClose" }, 5,
      { "int(void)", "int(i32 user,i32 type,i32 idx)", "int(i32 user,i32 type,i32 idx,void*)",
        "int(i32 handle,void*)", "int(i32 handle)" },
      { 1, 1, 1, 1, 1 } },

    { "libSceVideoOut.sprx", "own display output",
      "pinned ps5-payload-sdk; capture research hardware run 2 (loaded)", 0, 1,
      { "sceVideoOutOpen", "sceVideoOutSysReadCrc64" }, 2,
      { "int(i32 user,i32 type,i32 idx,const void*)", NULL },
      { 0, 0 } },

    { "libSceRemoteplay.sprx", "Remote Play status (not a frame source)",
      "pinned ps5-payload-sdk; capture research hardware run 2 (loaded, names present)", 0, 1,
      { "sceRemoteplayInitialize", "sceRemoteplayGetConnectionStatus", "sceRemoteplayProhibitStreaming" }, 3,
      { NULL, NULL, NULL },
      { 0, 0, 0 } },

    { "libSceScreenShot.sprx", "still capture (own surface, historically)",
      "OpenOrbis PS4 names; capture research hardware run 2 (loaded, names present)", 0, 0,
      { "sceScreenShotCapture", "sceScreenShotEnable", "sceScreenShotSetParam" }, 3,
      { NULL, NULL, NULL },
      { 0, 0, 0 } },

    { "libSceVideoRecording.sprx", "clip recording (QUARANTINED)",
      "capture research hardware run 2: dlopen did not return on 12.20", 1, 0,
      { "sceVideoRecordingOpen", "sceVideoRecordingStart", "sceVideoRecordingStop" }, 3,
      { NULL, NULL, NULL },
      { 0, 0, 0 } },

    { "libSceGameLiveStreaming.sprx", "broadcast (QUARANTINED)",
      "same load-and-start risk class as libSceVideoRecording", 1, 0,
      { "sceGameLiveStreamingEnable", "sceGameLiveStreamingStartBroadcast", "sceGameLiveStreamingStopBroadcast" }, 3,
      { NULL, NULL, NULL },
      { 0, 0, 0 } },
};

const ex_lib_entry *ex_lib_catalogue(size_t *count) {
    *count = sizeof(k_catalogue) / sizeof(k_catalogue[0]);
    return k_catalogue;
}

#define LIB_DIR "/system/common/lib"

void ex_module_libraries(const ex_backend *be, ex_run *run) {
    size_t count;
    const ex_lib_entry *cat = ex_lib_catalogue(&count);
    if (count > sizeof(run->lib) / sizeof(run->lib[0])) count = sizeof(run->lib) / sizeof(run->lib[0]);
    run->lib_count = count;

    for (size_t i = 0; i < count; i++) {
        const ex_lib_entry *e = &cat[i];
        ex_lib_probe *lp = &run->lib[i];
        memset(lp, 0, sizeof(*lp));

        char path[128];
        snprintf(path, sizeof(path), "%s/%s", LIB_DIR, e->library);
        lp->file_present = be->file_present(be->user, path);

        if (e->load_unsafe) {
            lp->load_attempted = 0;
            snprintf(lp->note, sizeof(lp->note), "quarantined: not dlopen'd on this firmware");
        } else {
            lp->load_attempted = 1;
            lp->load_ok = be->lib_lookup(be->user, e->library, e->symbols, lp->symbol_found,
                                         (size_t)e->symbol_count, lp->note, sizeof(lp->note)) == 0;
        }

        /* One probe per library (its load), then one per symbol (its look-up). */
        ex_status lib_st;
        ex_evidence lib_ev = e->hw_12_20 ? EX_EV_HARDWARE_VERIFIED : EX_EV_SOURCE_VERIFIED;
        if (e->load_unsafe)
            lib_st = EX_ST_SKIPPED;
        else if (lp->load_ok)
            lib_st = EX_ST_OK;
        else if (lp->file_present)
            lib_st = EX_ST_ERROR;
        else
            lib_st = EX_ST_NOT_AVAILABLE;

        ex_probe *p = ex_add_probe(run, 'B', e->library, lib_st, lib_ev);
        ex_probe_value(p, "%s | file %s%s", e->role, lp->file_present ? "present" : "absent",
                       e->load_unsafe ? ", quarantined (not loaded)"
                                      : (lp->load_ok ? ", loaded" : ", not loaded"));
        if (lp->note[0]) ex_probe_limit(p, "%s", lp->note);
        else ex_probe_limit(p, "Loading a library is not permission to use what it exports.");

        for (int si = 0; si < e->symbol_count; si++) {
            char sname[EX_NAME_MAX];
            snprintf(sname, sizeof(sname), "%s", e->symbols[si]);
            ex_status st;
            ex_evidence ev = e->sym_called_12_20[si] ? EX_EV_HARDWARE_VERIFIED
                                                     : (e->declared[si] ? EX_EV_SOURCE_VERIFIED : EX_EV_UNKNOWN);
            if (e->load_unsafe) st = EX_ST_SKIPPED;
            else if (!lp->load_ok) st = EX_ST_NOT_AVAILABLE;
            else st = lp->symbol_found[si] ? EX_ST_OK : EX_ST_NOT_AVAILABLE;

            ex_probe *sp = ex_add_probe(run, 'B', sname, st, ev);
            ex_probe_value(sp, "%s: name %s; declaration %s; called on 12.20 %s",
                           e->library,
                           e->load_unsafe ? "not looked up (quarantined)"
                                          : (lp->load_ok ? (lp->symbol_found[si] ? "present" : "absent")
                                                         : "not looked up"),
                           e->declared[si] ? e->declared[si] : "none",
                           e->sym_called_12_20[si] ? "yes (AKENO)" : "no");
            ex_probe_limit(sp, "dlsym confirms the symbol NAME only - not its signature, and not "
                               "that invoking it is safe or does what the name suggests.");
        }
    }
}
