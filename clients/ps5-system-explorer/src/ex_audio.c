/*
 * Module C: audio and microphone diagnostics (read-only baseline).
 *
 * The ultimate goal is independent PS5 voice chat while a game runs. This baseline does NOT open
 * an audio port and does NOT open the microphone. It records the one variable that prior AKENO
 * hardware tests showed to be decisive - the process context - together with which audio/mic
 * libraries and functions are available (from Module B), and it states the prior hardware evidence
 * and the open hypotheses separately, each with its evidence level. The optional active audio
 * probe is a separate, compile-time-gated build (AKENO_EXPLORER_AUDIO_PROBE) with its own review
 * and hardware plan; it is OFF here, so this ELF imports no sceAudioOut/In symbol.
 *
 * The scenario comparison (home screen / running game / AKENO Voice active / ShellUI) is produced
 * by the timeline module (Module E), which samples the context over time against user markers.
 */
#include "ex.h"

#include <stdio.h>
#include <string.h>

static const ex_lib_probe *find_lib(const ex_run *run, const char *name) {
    size_t count;
    const ex_lib_entry *cat = ex_lib_catalogue(&count);
    for (size_t i = 0; i < count && i < run->lib_count; i++)
        if (strcmp(cat[i].library, name) == 0) return &run->lib[i];
    return NULL;
}

void ex_module_audio(const ex_backend *be, ex_run *run) {
    (void)be;
    ex_context_kind ctx = run->context;
    int bg = ctx == EX_CTX_BACKGROUND;
    ex_probe *p;

    const ex_lib_probe *out = find_lib(run, "libSceAudioOut.sprx");
    const ex_lib_probe *in = find_lib(run, "libSceAudioIn.sprx");

    /* Whether this build even has an active probe. It does not, by design. */
    p = ex_add_probe(run, 'C', "active_audio_probe", EX_ST_SKIPPED, EX_EV_SOURCE_VERIFIED);
#ifdef AKENO_EXPLORER_AUDIO_PROBE
    ex_probe_value(p, "built WITH the experimental audio probe (AKENO_EXPLORER_AUDIO_PROBE)");
#else
    ex_probe_value(p, "disabled: this build opens no audio port and no microphone");
#endif
    ex_probe_limit(p, "The active probe is a separate build with its own review and hardware plan.");

    /* Question 1: is the current process recognised as audio-capable? */
    p = ex_add_probe(run, 'C', "process_audio_capable", EX_ST_NOT_AVAILABLE, EX_EV_HYPOTHESIS);
    ex_probe_value(p, "no interface exposes an 'audio-capable' flag for this process; context is %s",
                   ex_context_name(ctx));
    ex_probe_limit(p, "Prior AKENO hardware showed a foreground app is heard and a background payload "
                      "is not; this is a process-context correlation, not a queried capability.");

    /* Question 2: which permitted audio-port state can be retrieved read-only? */
    p = ex_add_probe(run, 'C', "audio_port_state_query", EX_ST_SKIPPED, EX_EV_SOURCE_VERIFIED);
    ex_probe_value(p, "sceAudioOutGetPortState name %s; not called here (needs an open port)",
                   out && out->symbol_found[4] ? "present" : (out ? "absent" : "library not loaded"));
    ex_probe_limit(p, "Reading port state requires opening a port, which the baseline does not do; "
                      "the argument list is agreed across sources but the struct size is unverified.");

    /* Output capability by context, from prior hardware. */
    if (bg) {
        p = ex_add_probe(run, 'C', "output_background", EX_ST_OK, EX_EV_HARDWARE_VERIFIED);
        ex_probe_value(p, "prior AKENO HW (FW 12.20): system-user main port opens and is paced, but is "
                          "not heard (home screen and in a game); foreground-user ports return 0x80260011");
        ex_probe_limit(p, "Measured by audio labs v1/v2 and Voice Alpha 0.1.0, not by this read-only run.");
    } else {
        p = ex_add_probe(run, 'C', "output_background", EX_ST_NOT_AVAILABLE,
                         ctx == EX_CTX_FOREGROUND ? EX_EV_HARDWARE_VERIFIED : EX_EV_UNKNOWN);
        if (ctx == EX_CTX_FOREGROUND)
            ex_probe_value(p, "this process is the foreground app; prior AKENO HW: foreground audio is audible");
        else
            ex_probe_value(p, "context is %s; the background-output result does not apply", ex_context_name(ctx));
        ex_probe_limit(p, "The background-output result applies only when this process is a background payload.");
    }

    /* Microphone capability by context, from prior hardware. */
    if (bg) {
        p = ex_add_probe(run, 'C', "microphone_background", EX_ST_OK, EX_EV_HARDWARE_VERIFIED);
        ex_probe_value(p, "prior AKENO HW (FW 12.20): sceAudioInOpen returns 0x809B0081 from a background "
                          "payload - the same refusal scePadOpen gives background payloads");
        ex_probe_limit(p, "Measured by Voice Alpha 0.1.0; this run does not open the microphone.");
    } else {
        p = ex_add_probe(run, 'C', "microphone_background", EX_ST_NOT_AVAILABLE,
                         ctx == EX_CTX_FOREGROUND ? EX_EV_HARDWARE_VERIFIED : EX_EV_UNKNOWN);
        if (ctx == EX_CTX_FOREGROUND)
            ex_probe_value(p, "foreground app; prior AKENO HW: the microphone opens at 16 kHz and follows speech");
        else
            ex_probe_value(p, "context is %s; the background-microphone result does not apply", ex_context_name(ctx));
        ex_probe_limit(p, "The background-microphone result applies only when this process is a background payload.");
    }

    /* Question 6: do failures correlate with user / context / port type / app state? */
    p = ex_add_probe(run, 'C', "failure_correlation", EX_ST_OK, EX_EV_HARDWARE_VERIFIED);
    ex_probe_value(p, "prior AKENO HW: byte-identical calls succeed in the foreground app and fail in a "
                      "background payload (output silent, mic 0x809B0081); the process type is the difference");
    ex_probe_limit(p, "Correlation across controlled comparisons; the enforcing system component is not documented.");

    /* The focus/attach interface: names only, never called. */
    p = ex_add_probe(run, 'C', "audio_focus_interface", EX_ST_SKIPPED, EX_EV_SOURCE_VERIFIED);
    ex_probe_value(p, "sceAudioOutGetFocusEnablePid / AttachToApplicationByPid exist as names%s; "
                      "no public declaration; not called",
                   out && out->symbol_found[5] ? " (resolved here)" : "");
    ex_probe_limit(p, "Calling them would make the system treat AKENO's audio as another app's "
                      "(spoofing); out of bounds and CI-forbidden.");

    /* Question 7: which missing measurements would distinguish the explanations? */
    p = ex_add_probe(run, 'C', "missing_measurements", EX_ST_NOT_AVAILABLE, EX_EV_HYPOTHESIS);
    ex_probe_value(p, "a read-only port-state read on an open port; a background-vs-foreground A/B of the "
                      "identical call; whether output is lost at routing or at the mixer");
    ex_probe_limit(p, "These need the optional active probe (separate build) or AKENO Native, not this baseline.");

    /* Library availability summary for the audio path. */
    p = ex_add_probe(run, 'C', "audio_libraries", (out && out->load_ok) ? EX_ST_OK : EX_ST_NOT_AVAILABLE,
                     EX_EV_HARDWARE_VERIFIED);
    ex_probe_value(p, "libSceAudioOut %s; libSceAudioIn file %s",
                   out ? (out->load_ok ? "loadable" : "not loaded") : "unknown",
                   in ? (in->file_present ? "present" : "absent") : "unknown");
    ex_probe_limit(p, "Presence of the libraries, not of a usable audio session for this process.");
}
