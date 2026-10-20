/*
 * Module A: process and application environment. Reads this process only (never scans unrelated
 * processes) and records, for every datum, whether it was measured, is unavailable, or is not
 * supported on FW 12.20 - with the evidence level and the limitation of each answer.
 */
#include "ex.h"

#include <stdio.h>
#include <string.h>

static const char *proc_state_name(int stat) {
    /* FreeBSD p_stat values (sys/proc.h); the PS5 kernel is FreeBSD-derived. */
    switch (stat) {
    case 1: return "idle";
    case 2: return "runnable";
    case 3: return "sleeping";
    case 4: return "stopped";
    case 5: return "zombie";
    case 6: return "waiting";
    case 7: return "locked";
    default: return "unknown";
    }
}

void ex_module_process(const ex_backend *be, ex_run *run) {
    ex_raw_proc raw;
    memset(&raw, 0, sizeof(raw));
    be->read_proc(be->user, &raw);
    ex_fill_sysinfo(&raw, be->platform(be->user), &run->sys);
    ex_sysinfo *s = &run->sys;
    run->context = ex_classify_context(s);
    int desktop = run->context == EX_CTX_DESKTOP;

    ex_probe *p;

    /* Firmware / OS version. */
    p = ex_add_probe(run, 'A', "firmware", s->have_fw && s->firmware[0] ? EX_ST_OK : EX_ST_NOT_AVAILABLE,
                     desktop ? EX_EV_UNKNOWN : EX_EV_HARDWARE_VERIFIED);
    if (s->have_fw && s->firmware[0])
        ex_probe_value(p, "%s (raw 0x%08x)", s->firmware, s->firmware_raw);
    else
        ex_probe_value(p, "kernel_get_fw_version unavailable");
    ex_probe_limit(p, "The firmware string; not a claim about any feature.");

    /* PID / PPID. */
    p = ex_add_probe(run, 'A', "pid", EX_ST_OK, EX_EV_HARDWARE_VERIFIED);
    ex_probe_value(p, "pid %d, ppid %d", s->pid, s->ppid);
    ex_probe_limit(p, "Identifies this process, not its privileges.");

    /* Process record (own process, read-only sysctl). */
    if (s->have_proc) {
        p = ex_add_probe(run, 'A', "process_record", EX_ST_OK,
                         desktop ? EX_EV_SOURCE_VERIFIED : EX_EV_HARDWARE_VERIFIED);
        ex_probe_value(p, "comm '%s', state %s (%d), rss %lld KiB", s->comm, proc_state_name(s->proc_stat),
                       s->proc_stat, s->rss_bytes >= 0 ? s->rss_bytes / 1024 : -1);
        ex_probe_limit(p, "sysctl KERN_PROC_PID for this PID only; unrelated processes are not read.");
    } else {
        p = ex_add_probe(run, 'A', "process_record", EX_ST_NOT_AVAILABLE, EX_EV_UNKNOWN);
        ex_probe_value(p, "the per-process query did not answer");
        ex_probe_limit(p, "Without it, state and start time are unknown.");
    }

    /* Executable identity (sceKernelGetAppInfo for getpid). */
    if (!s->have_appinfo) {
        p = ex_add_probe(run, 'A', "executable_identity", EX_ST_UNSUPPORTED_FW, EX_EV_SOURCE_VERIFIED);
        ex_probe_value(p, "sceKernelGetAppInfo is not exported by this firmware");
        ex_probe_limit(p, "A weak import; its absence only disables this probe.");
    } else if (s->appinfo_rc != 0) {
        p = ex_add_probe(run, 'A', "executable_identity", EX_ST_ERROR, EX_EV_HARDWARE_VERIFIED);
        ex_probe_rc(p, s->appinfo_rc);
        ex_probe_value(p, "sceKernelGetAppInfo(getpid) returned 0x%08x", (unsigned)s->appinfo_rc);
        ex_probe_limit(p, "No application info for this process (expected for a background payload).");
    } else {
        p = ex_add_probe(run, 'A', "executable_identity", EX_ST_OK, EX_EV_HARDWARE_VERIFIED);
        ex_probe_value(p, "app id 0x%08x, title id %s", s->own_app_id,
                       s->own_title_id[0] ? s->own_title_id : "(none at either known offset)");
        ex_probe_limit(p, "The app-info structure layout is undocumented; the title ID is read "
                          "conservatively at the two SDK-sample offsets (20/16).");
    }

    /* Foreground application identity. */
    if (!s->have_bigapp) {
        p = ex_add_probe(run, 'A', "foreground_app", EX_ST_UNSUPPORTED_FW, EX_EV_SOURCE_VERIFIED);
        ex_probe_value(p, "sceSystemServiceGetAppIdOfRunningBigApp is not exported");
    } else {
        p = ex_add_probe(run, 'A', "foreground_app", EX_ST_OK, EX_EV_HARDWARE_VERIFIED);
        if (s->bigapp_app_id > 0)
            ex_probe_value(p, "running big app id 0x%08x", (unsigned)s->bigapp_app_id);
        else
            ex_probe_value(p, "no running big app (rc 0x%08x): the home screen or no foreground app",
                           (unsigned)s->bigapp_app_id);
    }
    ex_probe_limit(p, "The app id of the foreground application; its focus state is not exposed.");

    /* Process context (the decisive variable for audio/microphone). */
    p = ex_add_probe(run, 'A', "process_context", run->context == EX_CTX_UNKNOWN ? EX_ST_NOT_AVAILABLE : EX_ST_OK,
                     desktop ? EX_EV_UNKNOWN : EX_EV_HARDWARE_VERIFIED);
    ex_probe_value(p, "%s", ex_context_name(run->context));
    ex_probe_limit(p, "Decided from own app id vs the running big app id; it does not grant or "
                      "measure any audio or device right.");

    /* Foreground user. */
    if (!s->have_user) {
        p = ex_add_probe(run, 'A', "foreground_user", EX_ST_UNSUPPORTED_FW, EX_EV_SOURCE_VERIFIED);
        ex_probe_value(p, "sceUserServiceGetForegroundUser is not available");
    } else if (s->user_rc != 0) {
        p = ex_add_probe(run, 'A', "foreground_user", EX_ST_ERROR, EX_EV_HARDWARE_VERIFIED);
        ex_probe_rc(p, s->user_rc);
        ex_probe_value(p, "sceUserServiceGetForegroundUser returned 0x%08x", (unsigned)s->user_rc);
    } else {
        p = ex_add_probe(run, 'A', "foreground_user", EX_ST_OK, EX_EV_HARDWARE_VERIFIED);
        ex_probe_value(p, "foreground user id 0x%08x", s->foreground_user);
    }
    ex_probe_limit(p, "The logged-in user; the system user (0xFF) is a separate identity for audio.");

    /* Resource limits. */
    if (s->have_rlimit) {
        p = ex_add_probe(run, 'A', "resource_limits", EX_ST_OK,
                         desktop ? EX_EV_SOURCE_VERIFIED : EX_EV_HARDWARE_VERIFIED);
        char as[48];
        if (s->rlimit_as_cur < 0) snprintf(as, sizeof(as), "unlimited/unknown");
        else snprintf(as, sizeof(as), "%lld MiB", s->rlimit_as_cur / (1024 * 1024));
        ex_probe_value(p, "open files %lld/%lld, address space %s", s->rlimit_nofile_cur, s->rlimit_nofile_max, as);
        ex_probe_limit(p, "getrlimit soft/hard limits for this process.");
    } else {
        p = ex_add_probe(run, 'A', "resource_limits", EX_ST_NOT_AVAILABLE, EX_EV_UNKNOWN);
        ex_probe_value(p, "getrlimit did not answer");
    }

    /* CPU time so far / peak memory. */
    if (s->have_rusage) {
        p = ex_add_probe(run, 'A', "resource_usage", EX_ST_OK,
                         desktop ? EX_EV_SOURCE_VERIFIED : EX_EV_HARDWARE_VERIFIED);
        ex_probe_value(p, "cpu user %.3f s, sys %.3f s, peak rss %lld KiB", s->cpu_user_s, s->cpu_sys_s,
                       s->maxrss_bytes >= 0 ? s->maxrss_bytes / 1024 : -1);
        ex_probe_limit(p, "getrusage(RUSAGE_SELF) at start-up; the timeline records scheduling gaps.");
    } else {
        p = ex_add_probe(run, 'A', "resource_usage", EX_ST_NOT_AVAILABLE, EX_EV_UNKNOWN);
        ex_probe_value(p, "getrusage did not answer");
    }
}
