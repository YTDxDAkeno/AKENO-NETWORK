/*
 * Module D: ShellUI and Game Base research. Source-backed findings, each classified
 * SOURCE_VERIFIED / HARDWARE_VERIFIED / HYPOTHESIS / UNKNOWN, with what it does NOT establish.
 * The commits were read locally for this phase; a missing feature in a source is not proof that it
 * is impossible on a console, only that no source evidence supports a build or safety promise.
 *
 * References (revisions verified for Phase 6):
 *   aydencharles/onionHEN      aaa4264 (v0.9.0) + b23ffe6 (host)      GPLv3
 *   OnionBuddies/onionHEN-plugin-sdk 72527c3                          GPLv3
 *   etaHEN/etaHEN              dafa13b                                GPLv3
 *   ZiZc3/LiveShell           1f87dcc (v1.4.1, on onionHEN v0.9.0-beta2) GPL-3.0
 *   Blaze414/ps5-theme        e29735f                                (see repo)
 *   earthonion/np-fake-signin 03403bc                                (see repo)
 *   ps5-payload-dev/offact    8c870eb                                (see repo)
 *   boykopovar/AnyPS5         eb75006                                GPL-2.0-or-later
 */
#include "ex.h"

#include <stddef.h>
#include <stdio.h>

typedef struct {
    const char *name;
    ex_evidence evidence;
    const char *finding;
    const char *limitation;
} ex_shellui_finding;

static const ex_shellui_finding k_findings[] = {
    { "onionHEN_plugin_pages", EX_EV_HARDWARE_VERIFIED,
      "onionHEN v0.9.0 exposes third-party plugin pages in the Settings/Toolbox (onion.ui ABI 1); the "
      "AKENO plugin AKEN00001 ran on the owner's 12.20 console and showed its page",
      "A Settings page reached by a shortcut is not a Control Center or Game Base integration." },

    { "onionHEN_notifications", EX_EV_SOURCE_VERIFIED,
      "plugins show plain-text toasts via onion_notify -> sceKernelSendNotificationRequest",
      "Plain text only; no interactive buttons through this path." },

    { "onionHEN_toolbox_shortcut", EX_EV_SOURCE_VERIFIED,
      "the host reads the pad itself (GamePad.GetData hook) for L2+R3, so the background-payload "
      "controller refusal (0x809B0081) does not apply to opening the Toolbox",
      "It opens the Toolbox root; it cannot jump straight to a specific plugin page." },

    { "liveshell_animated_background", EX_EV_SOURCE_VERIFIED,
      "LiveShell v1.4.1 injects a module into SceShellUI (OnionHEN injector + detour of "
      "PUI.Application.Update) and drives Sony's own PUI.UI3.VideoView via Mono to play a video on "
      "the BGLayer; videos are nullfs-mounted into the ShellUI sandboxes at /system_ex/vsh_asset/wallpaper",
      "This is host-level code inside SceShellUI (Mono reflection), not a plugin API; it is firmware- "
      "and build-specific and out of bounds for AKENO's rules (no ShellUI host injection)." },

    { "liveshell_game_pause", EX_EV_SOURCE_VERIFIED,
      "LiveShell pauses its video while a game runs, detecting the game with "
      "sceSystemServiceGetAppIdOfRunningBigApp + sceSystemServiceGetAppTitleId (1 Hz) - the same "
      "read-only queries AKENO uses",
      "Confirms the game-detection method AKENO already uses; says nothing about audio rights." },

    { "liveshell_bgm_handling", EX_EV_SOURCE_VERIFIED,
      "LiveShell's audio runs INSIDE SceShellUI (Mono VideoPlayer.SetVolume/SetIsMuted); the player "
      "suppresses the system BGM. This is ShellUI's audio session, not a background payload's",
      "It is audible because it runs in SceShellUI (a system app), not from an ELF-loader payload like AKENO." },

    { "ps5_theme_static_overlay", EX_EV_SOURCE_VERIFIED,
      "ps5-theme applies assets as read-only, in-memory nullfs overlays; ShellUI reads system assets "
      "through its own sandbox views, not a direct path",
      "Static media only; it cannot bind dynamic AKENO data (friends, messages) into Game Base." },

    { "etahen_shellcore_offsets", EX_EV_SOURCE_VERIFIED,
      "etaHEN's apply_shellcore_patches selects only known firmware offset sets and aborts otherwise; "
      "this snapshot has no 12.20 case",
      "A non-compatibility signal for 12.20 ShellCore patching in this source, not merely a missing test." },

    { "np_party_states", EX_EV_SOURCE_VERIFIED,
      "etaHEN lists NP party error states (not initialized, signed out, server error, voice not "
      "enabled): party functions can require several non-local states",
      "An error table, not a party implementation, protocol or an alternative to Sony's backend." },

    { "np_fake_signin_ps5", EX_EV_SOURCE_VERIFIED,
      "np-fake-signin's PS5 path writes local NP files and registry state; its ShellCore memory patch "
      "is compiled PS4-only (#ifndef PS5)",
      "Local persistence, not a dynamically updated ShellCore social context, and no Game Base data adapter. "
      "These writes are forbidden by AKENO's rules." },

    { "offact_local_activation", EX_EV_SOURCE_VERIFIED,
      "offact sets a local account id/type/flags in the registry and states it does NOT enable online "
      "PSN gaming",
      "A local user-slot-to-account mapping, not a social backend, and it changes account registry state "
      "(forbidden here)." },

    { "anyps5_system_service_status", EX_EV_SOURCE_VERIFIED,
      "AnyPS5 (PC reimplementation) models SystemServiceStatus {event_num, is_system_ui_overlaid, "
      "is_in_background_execution, is_vr_play_area_overlaid}; its focus-pid names are stubs only",
      "An emulator model inferred from titles' calls, not a PS5 console measurement." },

    { "game_base_backend", EX_EV_UNKNOWN,
      "no identified interface lets homebrew supply friends, presence, messages, parties or voice to "
      "the system Game Base; it is PSN-identity-backed",
      "'Unknown/not available through identified interfaces' is not a proof of impossibility, and NP "
      "sign-in spoofing is not a working alternative Game Base." },

    { "game_base_local_audio_vs_signaling", EX_EV_UNKNOWN,
      "no source distinguishes a local party-audio facility from Sony's network signaling in a way a "
      "payload could use; party sessions appear bound to PSN session identity",
      "No open interface, no signaling/mixer evidence, and no 12.20 support were found." },
};

void ex_module_shellui(ex_run *run) {
    size_t n = sizeof(k_findings) / sizeof(k_findings[0]);
    for (size_t i = 0; i < n; i++) {
        const ex_shellui_finding *f = &k_findings[i];
        ex_status st = f->evidence == EX_EV_UNKNOWN ? EX_ST_NOT_AVAILABLE : EX_ST_OK;
        ex_probe *p = ex_add_probe(run, 'D', f->name, st, f->evidence);
        ex_probe_value(p, "%s", f->finding);
        ex_probe_limit(p, "%s", f->limitation);
    }
}
