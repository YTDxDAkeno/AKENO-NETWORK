/*
 * AKENO System Explorer 6.1 (EXPERIMENTAL, docs/PHASE6_AKENO_SYSTEM_EXPLORER_DE.md and
 * docs/PHASE6_1_PROCESS_AUDIO_RESEARCH_DE.md).
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

#define AKN_EX_VERSION "6.1.0"
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
    ex_proc_query pq;             /* how the per-process sysctl went (6.1) */
    int firmware_reviewed;        /* the firmware is 12.20, the one the evidence comes from */
} ex_sysinfo;

/* ---- 6.1: why the per-process query did (not) answer ---------------------------------------- */

typedef enum {
    EX_PQ_DECODED = 0,            /* layout-compatible record for this PID, decoded */
    EX_PQ_NOT_ATTEMPTED,          /* the backend has no per-process query (PC build without it) */
    EX_PQ_SIZE_QUERY_DENIED,      /* the size query failed with EPERM/EACCES */
    EX_PQ_SIZE_QUERY_UNSUPPORTED, /* ENOENT/EOPNOTSUPP/EINVAL: node or invocation not supported */
    EX_PQ_NO_SUCH_PROCESS,        /* ESRCH */
    EX_PQ_SIZE_QUERY_FAILED,      /* another errno */
    EX_PQ_RECORD_TOO_LARGE,       /* the kernel's record exceeds the backend's bound: not read */
    EX_PQ_READ_FAILED,            /* the read failed (errno recorded) */
    EX_PQ_EMPTY,                  /* the read returned no record */
    EX_PQ_LAYOUT_MISMATCH,        /* ki_structsize differs from the SDK's kinfo_proc: not decoded */
    EX_PQ_PID_MISMATCH            /* sizes agree but ki_pid is not this PID: not decoded */
} ex_pq_verdict;

const char *ex_pq_verdict_name(ex_pq_verdict v);
/* Classifies a per-process query (unit-tested); writes a precise one-line explanation. */
ex_pq_verdict ex_pq_classify(const ex_proc_query *q, int own_pid, char *why, size_t why_len);
const char *ex_errno_kind_name(ex_errno_kind k);

/* ---- 6.1: title ID of the observed running app -------------------------------------------- */

typedef enum {
    EX_TITLE_OK = 0,              /* a well-formed title ID ([A-Z]{4}[0-9]{5}, NUL-terminated) */
    EX_TITLE_NO_FUNCTION,         /* sceSystemServiceGetAppTitleId is not exported */
    EX_TITLE_DISABLED,            /* switched off by the owner (no-title-query file) */
    EX_TITLE_BUDGET,              /* the per-session call budget is used up */
    EX_TITLE_GUARD_STOP,          /* an earlier call wrote outside its window: never called again */
    EX_TITLE_NO_APP,              /* no running big app id to ask about */
    EX_TITLE_ERROR,               /* the call returned an error code */
    EX_TITLE_EMPTY,               /* rc 0 but an empty string */
    EX_TITLE_MALFORMED,           /* rc 0 but not a title-ID-shaped, NUL-terminated string */
    EX_TITLE_OVERRUN              /* the call wrote outside its window (the query is then disabled) */
} ex_title_status;

const char *ex_title_status_name(ex_title_status s);
/* Interprets one query answer (unit-tested). title receives the ID for EX_TITLE_OK, else "". */
ex_title_status ex_title_classify(const ex_title_query *q, char *title, size_t title_len);
/* A title ID's naming-convention class: "PS5 title", "PS4 title", "system app", "other". */
const char *ex_title_class(const char *title);

#define EX_TITLE_BUDGET_MAX 32u   /* sceSystemServiceGetAppTitleId calls per session, at most */
#define EX_TITLE_RECHECK_POLLS 15u/* re-ask about a vanished app every 15 polls (~15 s), within budget */

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
    EX_EVT_STOP,
    EX_EVT_TITLE,                 /* 6.1: a title-ID lookup for the running app (result in text) */
    EX_EVT_APP_STATE,             /* 6.1: after the app vanished: still answers / no longer answers */
    EX_EVT_CONTROL                /* 6.1: control-file housekeeping (replaced file, partial line) */
} ex_event_kind;

const char *ex_event_kind_name(ex_event_kind k);

typedef struct {
    uint64_t t_us;                /* monotonic, relative to the session start */
    long long unix_s;            /* wall clock, for correlation */
    ex_event_kind kind;
    int32_t bigapp_app_id;        /* context at the time */
    ex_context_kind context;
    int gap_ms;                   /* for EX_EVT_SCHED_GAP */
    int32_t from_app_id;          /* for EX_EVT_CONTEXT_CHANGE */
    char title_id[16];            /* the running app's verified title ID at the time, if any */
    char source[16];              /* for EX_EVT_MARKER: "control" or "relaunch" */
    char text[120];               /* marker text (redacted) or a short note */
} ex_event;

/* ---- 6.1: foreground application sessions ------------------------------------------------- */

typedef enum {
    EX_FG_END_OPEN = 0,           /* still the running big app when the Explorer ended */
    EX_FG_END_CHANGED,            /* another app id was reported instead */
    EX_FG_END_NOT_REPORTED        /* no running big app was reported any more */
} ex_fg_end;

typedef enum {
    EX_FG_AFTER_UNKNOWN = 0,      /* not checked, or the check could not decide */
    EX_FG_AFTER_STILL_ANSWERS,    /* the title query for its id still returns the same title ID */
    EX_FG_AFTER_GONE              /* the title query for its id no longer returns that title ID */
} ex_fg_after;

typedef struct {
    int32_t app_id;               /* the runtime id (NOT a permanent title ID) */
    uint64_t start_us, end_us;    /* relative to the session start */
    ex_fg_end end;
    ex_title_status title_status;
    int32_t title_rc;             /* when title_status == EX_TITLE_ERROR */
    char title_id[16];
    ex_fg_after after;            /* only for EX_FG_END_NOT_REPORTED */
    uint64_t after_us;            /* when the after-state was decided */
    int open;                     /* 1 while it is the running app (end_us not final) */
} ex_fg_session;

#define EX_FG_MAX 32

/* Poll statistics (6.1). */
typedef struct {
    unsigned polls;
    uint64_t interval_min_us, interval_max_us, interval_sum_us; /* between consecutive polls */
    unsigned intervals;
    uint64_t latency_min_us, latency_max_us, latency_sum_us;    /* time inside sample_context */
    unsigned bigapp_none;         /* samples with no running big app (not a failure) */
    unsigned bigapp_unavailable;  /* samples without the function */
    int32_t bigapp_last_none_raw; /* the raw answer of the last "none" sample */
    unsigned user_failures;       /* samples where the foreground-user query failed */
    int32_t user_last_rc;
    unsigned title_calls;         /* sceSystemServiceGetAppTitleId calls made */
    unsigned title_failures;      /* calls that did not give a well-formed title ID */
} ex_poll_stats;

/* ---- 6.1: instance / crash recovery (decided in main, recorded here) ---------------------- */

typedef enum {
    EX_LOCK_ACQUIRED = 0,         /* an advisory lock (flock) was taken: no live holder */
    EX_LOCK_HELD,                 /* another live process holds the advisory lock */
    EX_LOCK_UNSUPPORTED,          /* flock is not supported here: heartbeat decides alone */
    EX_LOCK_ERROR                 /* the lock file could not be opened */
} ex_lock_result;

const char *ex_lock_result_name(ex_lock_result r);

/* What a starting Explorer found (filled by main from the files in the output directory). */
typedef struct {
    int lock_file_existed;
    ex_lock_result lock;
    int status_present;           /* status.json exists and parsed */
    int status_running;           /* its state is RUNNING */
    long long heartbeat_unix;     /* its last heartbeat */
    long long now_unix;
    char status_session[48];      /* its session id */
    char lock_session[48];        /* the session id written in explorer.lock */
} ex_instance_probe;

typedef enum {
    EX_INST_START = 0,            /* nothing running: a new session */
    EX_INST_START_RECOVERED,      /* a previous session was interrupted (or a lock was left): new session */
    EX_INST_MARKER,               /* a live session runs: hand it a marker, start no session */
    EX_INST_REFUSE                /* a live holder that does not answer: start nothing */
} ex_instance_action;

#define EX_HEARTBEAT_S 10u        /* status.json is rewritten at least this often */
#define EX_HEARTBEAT_STALE_S 45   /* older (or in the future by more) = not alive */

const char *ex_instance_action_name(ex_instance_action a);
/* The single decision rule for a starting Explorer (unit-tested, docs/PHASE6_1 G). */
ex_instance_action ex_instance_decide(const ex_instance_probe *p, char *why, size_t why_len);

/* Parses the few fields of a status.json the decision needs (unit-tested). Returns 0 on success. */
int ex_status_parse(const char *text, int *running, long long *heartbeat_unix, char *session, size_t session_len);

typedef struct {
    char id[48];                  /* "<start unix>-<pid>" */
    ex_instance_action action;    /* START or START_RECOVERED for a running session */
    ex_lock_result lock;
    char recovery[200];           /* what was found and done (interrupted session, stale lock, ...) */
    long long stale_control_bytes;/* -1 none; else bytes of a leftover control file moved aside, not applied */
    int previous_reports_moved;   /* report files of the previous session moved to previous/ */
    int title_query_enabled;      /* 0 when /data/akeno-system-explorer/no-title-query exists */
} ex_session_info;

#define EX_EVENTS_MAX 2048

/* ---- The run ---------------------------------------------------------------------------------- */

struct ex_run_s;

typedef struct {
    int quick;                    /* PC tests / a quick run: short session */
    unsigned session_s;           /* default session length (default 300, hard max EX_SESSION_MAX_S) */
    unsigned poll_ms;             /* context poll interval (default 1000) */
    ex_session_info session;      /* 6.1: set by main before ex_execute */
    /* 6.1: called at the start, every EX_HEARTBEAT_S and at the end (status.json). Returns
     * nonzero when the session must end at once because another session took over the lock. */
    int (*checkpoint)(void *ctx, const struct ex_run_s *run, int final);
    void *checkpoint_ctx;
} ex_options;

#define EX_SESSION_DEFAULT_S 300u
#define EX_SESSION_MAX_S 1800u
#define EX_POLL_DEFAULT_MS 1000u
#define EX_GAP_FACTOR 3           /* a gap > poll_ms * EX_GAP_FACTOR is recorded as a scheduling gap */

typedef struct ex_run_s {
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

    /* 6.1 */
    ex_fg_session fg[EX_FG_MAX];
    size_t fg_count;
    int fg_truncated;
    ex_poll_stats poll;
    ex_usage usage_end;           /* getrusage at the end of the session */
    long long maxrss_seen;        /* the highest peak RSS read during the session */
    unsigned markers_relaunch;    /* markers handed over by a relaunched ELF */
    unsigned control_resets;      /* the control file was replaced/truncated mid-session */
    unsigned control_partial;     /* a last line without newline was applied after it settled */
    int superseded;               /* another session took over the lock: ended early */
    int title_guard_stop;         /* the title query wrote outside its window: disabled */
    uint64_t last_heartbeat_us;
    int checkpoint_failures;      /* status.json writes that failed */
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
/* system-summary.txt: a German summary (6.1) followed by the full probe list (screenshot-friendly). */
size_t ex_report_summary_txt(const ex_run *run, char *out, size_t len);
/* status.json (6.1): the live heartbeat; state RUNNING, COMPLETED or SUPERSEDED. */
size_t ex_report_status_json(const ex_run *run, const char *state, long long now_unix, char *out, size_t len);

/* "mm:ss" for a session-relative time (unit-tested). */
void ex_fmt_mmss(uint64_t t_us, char *out, size_t len);

#endif
