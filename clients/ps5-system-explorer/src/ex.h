/*
 * AKENO System Explorer 6.0 (EXPERIMENTAL, docs/PHASE6_AKENO_SYSTEM_EXPLORER_DE.md).
 *
 * A standalone, read-only PS5 diagnostic payload. It collects evidence about the current process
 * and application environment from legitimate, available system interfaces, so the next
 * engineering decision about Game Base / in-game voice rests on measured facts instead of
 * speculation. It runs as a background payload (the same process type as the AKENO daemon).
 *
 * Modules (every one records what it could NOT establish next to what it measured):
 *   A  process and application environment (own PID/PPID, context, uptime, resource limits)
 *   B  system libraries and exported symbols (looked up only, never called)
 *   C  audio and microphone diagnostics (read-only; the baseline opens no port and no microphone)
 *   D  ShellUI and Game Base research (source-backed findings, classified)
 *   E  a low-overhead event timeline with user-entered markers (bounded, clean shutdown)
 *   F  structured, machine-readable reports
 *
 * Everything that touches the console is behind ex_backend (ex_backend_ps5.c, or ex_backend_host.c
 * for the PC tests). The modules and the report generator are portable and unit-tested. A passing
 * PC run is never evidence of PS5 behaviour.
 *
 * Design rules (CI-enforced, see console/check-explorer.sh):
 *   - no app launch/kill, mount, ptrace, kernel memory helpers, pad focus, registry, install util;
 *   - no audio output and no microphone in the default build (the optional probe is a separate,
 *     compile-time-gated build with its own review and hardware plan: AKENO_EXPLORER_AUDIO_PROBE);
 *   - unrelated processes are never scanned: Module A reads this process only;
 *   - no value is invented: a missing measurement is reported as NOT_AVAILABLE / UNSUPPORTED.
 */
#ifndef AKN_EX_H
#define AKN_EX_H

#include <stddef.h>
#include <stdint.h>

#include "ex_backend.h"

#define AKN_EX_VERSION "6.0.0"
#ifndef AKN_EX_BUILD_ID
#define AKN_EX_BUILD_ID "dev"
#endif

/* How strongly a recorded statement is backed. Matches docs/PHASE6 evidence levels. */
typedef enum {
    EX_EV_UNKNOWN = 0,       /* not established by anything the Explorer has */
    EX_EV_SOURCE_VERIFIED,   /* declared / behaviour read in a cited source or the pinned SDK */
    EX_EV_HARDWARE_VERIFIED, /* measured on the owner's PS5 on FW 12.20 (by the Explorer, or prior AKENO) */
    EX_EV_HYPOTHESIS         /* consistent with an observation, not proven */
} ex_evidence;

/* The outcome of one probe. A value exists only for EX_ST_OK. */
typedef enum {
    EX_ST_OK = 0,            /* a value was measured in this run */
    EX_ST_NOT_AVAILABLE,     /* the interface or datum is not available in this context */
    EX_ST_UNSUPPORTED_FW,    /* not exported / not supported on FW 12.20 */
    EX_ST_SKIPPED,           /* deliberately not run (quarantined, or the probe is disabled) */
    EX_ST_ERROR              /* a call returned an error code (rc is set) */
} ex_status;

const char *ex_evidence_name(ex_evidence e);
const char *ex_status_name(ex_status s);

#define EX_NAME_MAX 56
#define EX_VALUE_MAX 200
#define EX_LIMIT_MAX 200

/* One recorded observation. Everything shown in a report is a probe. */
typedef struct {
    char module;                  /* 'A'..'F' */
    char name[EX_NAME_MAX];
    ex_status status;
    ex_evidence evidence;
    int has_rc;
    int32_t rc;                   /* a return / error code, when the probe made a call */
    char value[EX_VALUE_MAX];     /* the observed value (already redacted) */
    char limitation[EX_LIMIT_MAX];/* what this probe does NOT establish */
} ex_probe;

#define EX_PROBES_MAX 192

/* ---- Module A: process and application environment ------------------------------------------- */

typedef struct {
    char platform[32];            /* "PlayStation 5" or "PC simulation (not a PS5)" */
    int have_fw;
    uint32_t firmware_raw;
    char firmware[16];            /* "12.20" or "" */
    int pid, ppid;
    char comm[32];                /* process command name (sysctl ki_comm) */
    int have_proc;                /* sysctl for this process succeeded */
    int proc_stat;                /* ki_stat */
    long long start_unix;         /* ki_start seconds, 0 if unknown */
    long long rss_bytes;          /* resident set size, -1 if unknown */
    int have_appinfo;
    int32_t appinfo_rc;
    uint32_t own_app_id;
    char own_title_id[16];
    int have_bigapp;
    int32_t bigapp_app_id;
    int have_user;
    int32_t user_rc;
    uint32_t foreground_user;
    int have_rlimit;
    long long rlimit_nofile_cur, rlimit_nofile_max;
    long long rlimit_as_cur;      /* address space soft limit, -1 unlimited/unknown */
    int have_rusage;
    double cpu_user_s, cpu_sys_s; /* getrusage(RUSAGE_SELF) */
    long long maxrss_bytes;
} ex_sysinfo;

/* The process context, decided from raw system answers (unit-tested). */
typedef enum {
    EX_CTX_UNKNOWN = 0,
    EX_CTX_FOREGROUND,            /* this process is the running foreground application */
    EX_CTX_BACKGROUND,            /* another application (or none) is in the foreground */
    EX_CTX_DESKTOP                /* PC build */
} ex_context_kind;

const char *ex_context_name(ex_context_kind k);
ex_context_kind ex_classify_context(const ex_sysinfo *s);
/* Decodes a backend's raw process answers into the reportable ex_sysinfo (unit-tested). */
void ex_fill_sysinfo(const ex_raw_proc *raw, const char *platform, ex_sysinfo *s);

/* ---- Module B: libraries and exported symbols ------------------------------------------------ */

#define EX_LIB_SYMBOLS_MAX 12

typedef struct {
    const char *library;          /* e.g. "libSceAudioOut.sprx" */
    const char *role;             /* short human description */
    const char *source;           /* where the names come from */
    int load_unsafe;              /* quarantined: never dlopen (STATIC presence only) */
    int hw_12_20;                 /* this library has been dlopen'd on FW 12.20 in an AKENO build */
    const char *symbols[EX_LIB_SYMBOLS_MAX];
    int symbol_count;
    /* a supported declaration (argument types) is known for this symbol index */
    const char *declared[EX_LIB_SYMBOLS_MAX];
    /* this symbol has been *successfully called* on FW 12.20 in an AKENO build */
    int sym_called_12_20[EX_LIB_SYMBOLS_MAX];
} ex_lib_entry;

const ex_lib_entry *ex_lib_catalogue(size_t *count);

typedef struct {
    int file_present;             /* the .sprx exists under /system/common/lib */
    int load_attempted;
    int load_ok;
    int symbol_found[EX_LIB_SYMBOLS_MAX];
    char note[160];
} ex_lib_probe;

/* ---- Module E: event timeline ---------------------------------------------------------------- */

typedef enum {
    EX_EVT_START = 0,
    EX_EVT_SAMPLE,                /* a periodic context sample */
    EX_EVT_CONTEXT_CHANGE,        /* the running app or foreground user changed */
    EX_EVT_SCHED_GAP,             /* a scheduling gap larger than expected (observation only) */
    EX_EVT_MARKER,                /* a user-entered marker */
    EX_EVT_EXTEND,
    EX_EVT_STOP
} ex_event_kind;

const char *ex_event_kind_name(ex_event_kind k);

typedef struct {
    uint64_t t_us;                /* monotonic, relative to the session start */
    long long unix_s;            /* wall clock, for correlation */
    ex_event_kind kind;
    int32_t bigapp_app_id;        /* context at the time */
    ex_context_kind context;
    int gap_ms;                   /* for EX_EVT_SCHED_GAP */
    char text[120];               /* marker text (redacted) or a short note */
} ex_event;

#define EX_EVENTS_MAX 2048

/* ---- The run ---------------------------------------------------------------------------------- */

typedef struct {
    int quick;                    /* PC tests / a quick run: short session */
    unsigned session_s;           /* default session length (default 300, hard max EX_SESSION_MAX_S) */
    unsigned poll_ms;             /* context poll interval (default 1000) */
} ex_options;

#define EX_SESSION_DEFAULT_S 300u
#define EX_SESSION_MAX_S 1800u
#define EX_POLL_DEFAULT_MS 1000u
#define EX_GAP_FACTOR 3           /* a gap > poll_ms * EX_GAP_FACTOR is recorded as a scheduling gap */

typedef struct {
    ex_options opt;
    char build_id[32];

    ex_sysinfo sys;
    ex_context_kind context;

    ex_lib_probe lib[16];
    size_t lib_count;

    ex_probe probe[EX_PROBES_MAX];
    size_t probe_count;

    ex_event event[EX_EVENTS_MAX];
    size_t event_count;
    int events_truncated;

    /* timeline summary */
    uint64_t started_us, ended_us;
    long long started_unix;
    unsigned context_changes;
    unsigned markers;
    unsigned sched_gaps;
    int gap_max_ms;
    unsigned requested_session_s; /* after any extend */
    int stopped_early;
} ex_run;

/* Record one probe into run (truncates silently past EX_PROBES_MAX; the count still grows). */
ex_probe *ex_add_probe(ex_run *run, char module, const char *name, ex_status st, ex_evidence ev);
void ex_probe_value(ex_probe *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void ex_probe_rc(ex_probe *p, int32_t rc);
void ex_probe_limit(ex_probe *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Record one event (drops past EX_EVENTS_MAX and sets events_truncated). */
ex_event *ex_add_event(ex_run *run, ex_event_kind kind, uint64_t t_us, long long unix_s);

/* Modules. Each fills run from the backend; all are portable and side-effect free besides run. */
void ex_module_process(const ex_backend *be, ex_run *run);
void ex_module_libraries(const ex_backend *be, ex_run *run);
void ex_module_audio(const ex_backend *be, ex_run *run);
void ex_module_shellui(ex_run *run);

/* The timeline module runs the timed session (blocking, minutes). */
void ex_module_timeline(const ex_backend *be, ex_run *run);

/* Runs modules A-D, then the timeline (E), filling run. */
void ex_execute(const ex_backend *be, const ex_options *opt, ex_run *run);

/* ---- Module F: reports ------------------------------------------------------------------------ */

/* report.json: the whole run (system, context, modules, timeline summary, every probe). */
size_t ex_report_json(const ex_run *run, char *out, size_t len);
/* modules.json: the per-module probe lists. */
size_t ex_report_modules_json(const ex_run *run, char *out, size_t len);
/* audio-diagnostics.json: Module C in detail (context-decided audio capability, prior evidence). */
size_t ex_report_audio_json(const ex_run *run, char *out, size_t len);
/* events.jsonl: one JSON object per line, in order. */
size_t ex_report_events_jsonl(const ex_run *run, char *out, size_t len);
/* system-summary.txt: a human-readable summary (screenshot-friendly). */
size_t ex_report_summary_txt(const ex_run *run, char *out, size_t len);

#endif
