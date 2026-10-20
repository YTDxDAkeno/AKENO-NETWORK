/*
 * The Explorer's platform seam. The PS5 implementation (ex_backend_ps5.c) is the only code that
 * declares or calls console system functions; the PC simulation (ex_backend_host.c) models them
 * with failure injection for the tests. Every call here is read-only.
 */
#ifndef AKN_EX_BACKEND_H
#define AKN_EX_BACKEND_H

#include <stddef.h>
#include <stdint.h>

/* errno classes, decided by the backend with its own platform's constants (FreeBSD and Linux
 * number some of them differently), so the portable code can classify a failure. */
typedef enum {
    EX_ERRNO_NONE = 0,
    EX_ERRNO_NOMEM,               /* ENOMEM: the buffer was smaller than the kernel's answer */
    EX_ERRNO_PERM,                /* EPERM */
    EX_ERRNO_ACCES,               /* EACCES */
    EX_ERRNO_SRCH,                /* ESRCH: no such process */
    EX_ERRNO_INVAL,               /* EINVAL */
    EX_ERRNO_NOENT,               /* ENOENT: the sysctl node does not exist */
    EX_ERRNO_OPNOTSUPP,           /* EOPNOTSUPP / ENOTSUP / ENOSYS */
    EX_ERRNO_FAULT,               /* EFAULT */
    EX_ERRNO_OTHER
} ex_errno_kind;

/*
 * The per-process sysctl (KERN_PROC_PID, this process only), done in two steps: a size query
 * (no buffer) and a read into a bounded buffer. Everything the kernel answered is recorded, so a
 * failure can be told apart (permission, unsupported node, structure-size mismatch, ...).
 */
typedef struct {
    int attempted;
    int size_rc;                  /* sysctl(mib, NULL, &size) */
    int size_errno;               /* raw errno after a failed size query (0 if it succeeded) */
    ex_errno_kind size_errno_kind;
    unsigned long long size_reported; /* the size the kernel reported (0 if the query failed) */
    unsigned long long requested; /* the buffer size passed to the read */
    int read_rc;
    int read_errno;
    ex_errno_kind read_errno_kind;
    unsigned long long returned;  /* bytes the kernel reported back from the read */
    unsigned long long buffer_bound; /* the backend's buffer (never exceeded) */
    unsigned long long sdk_size;  /* sizeof(struct kinfo_proc) in the pinned SDK headers */
    int have_structsize;          /* at least 4 bytes came back */
    int structsize;               /* ki_structsize (the first field) as the kernel wrote it */
    int have_pid_at_sdk_offset;   /* enough bytes for ki_pid at the SDK's offset */
    int pid_at_sdk_offset;        /* the value there (compared with getpid(), never trusted alone) */
    int decoded;                  /* the record was decoded (only when fully layout-compatible) */
} ex_proc_query;

/* CPU / memory of this process (getrusage RUSAGE_SELF). */
typedef struct {
    int have;
    double cpu_user_s, cpu_sys_s;
    long long maxrss_bytes;       /* peak resident set size so far (ru_maxrss * 1024) */
} ex_usage;

/* Raw process / application answers, filled by the backend and interpreted by Module A. */
typedef struct {
    int have_fw;
    uint32_t firmware_raw;        /* kernel_get_fw_version(), BCD 0xMMmm0000 */

    int pid, ppid;

    int have_proc;                /* the per-process sysctl (own PID) was decoded */
    char comm[32];                /* ki_comm */
    int proc_stat;                /* ki_stat */
    long long start_unix;         /* ki_start seconds */
    long long rss_bytes;          /* ki_rssize * page size, -1 unknown */
    ex_proc_query pq;             /* how the per-process query went (always filled on the PS5) */

    int have_appinfo_fn;          /* sceKernelGetAppInfo resolved */
    int32_t appinfo_rc;           /* its result for getpid() */
    unsigned char appinfo_raw[64];/* first bytes of its answer */

    int have_bigapp_fn;           /* sceSystemServiceGetAppIdOfRunningBigApp resolved */
    int32_t bigapp_app_id;        /* its result */

    int have_user_fn;             /* sceUserServiceGetForegroundUser resolved */
    int32_t user_rc;
    uint32_t foreground_user;

    int have_rlimit;
    long long rlimit_nofile_cur, rlimit_nofile_max;
    long long rlimit_as_cur;      /* -1 = unlimited or unknown */

    int have_rusage;
    double cpu_user_s, cpu_sys_s;
    long long maxrss_bytes;
} ex_raw_proc;

/* One live context sample (cheap, read-only). */
typedef struct {
    int have_bigapp_fn;
    int32_t bigapp;               /* >0: a running big app's runtime id; <=0: none (raw answer kept) */
    int have_user_fn;
    int32_t user_rc;
    uint32_t user;                /* valid when user_rc == 0 */
} ex_ctx_sample;

/*
 * One sceSystemServiceGetAppTitleId(app_id, buf) answer. The function takes no length, so the
 * backend passes a zeroed window at least as large as the largest buffer any source caller uses
 * (see ex_backend_ps5.c) inside guard bytes. The guard bytes only DETECT an overrun; they are not
 * the safety argument (the agreed declaration and buffer sizes are, docs/PHASE6_1).
 */
#define EX_TITLE_WINDOW 256
#define EX_TITLE_RAW_KEEP 32

typedef struct {
    int have_fn;                  /* the weak import resolved */
    int called;                   /* the call was made (else: disabled, budget or guard stop) */
    int32_t rc;
    unsigned char raw[EX_TITLE_RAW_KEEP]; /* the first bytes of the window after the call */
    int nul_at;                   /* index of the first NUL in the window, -1 if none */
    int written;                  /* bytes of the window that differ from the zero fill (lower bound) */
    int guard_ok;                 /* the guard bytes around the window are intact */
} ex_title_query;

typedef struct ex_backend {
    const char *name;             /* "PS5" or "PC simulation (not a PS5)" */
    void *user;

    uint64_t (*now_us)(void *u);
    void (*sleep_ms)(void *u, unsigned ms);
    long long (*unix_time)(void *u);
    int (*notify)(void *u, const char *text);

    /* Module A: this process only (never scans unrelated processes). */
    void (*read_proc)(void *u, ex_raw_proc *out);
    /* The platform name for the report. */
    const char *(*platform)(void *u);

    /* Module B: look-up only. file_present() stats a library file; lib_lookup() dlopens a library
     * and dlsyms each name (addresses are compared with NULL and never called). */
    int (*file_present)(void *u, const char *path);
    int (*lib_lookup)(void *u, const char *library, const char *const *names, int *found,
                      size_t count, char *note, size_t note_len);

    /* Module E: read appended control commands (a loopback-free control file). Returns the number
     * of bytes read into buf (0 if none / not available). *reset is set to 1 when the file was
     * replaced or truncated since the last call (reading restarted at its beginning). */
    int (*read_control)(void *u, char *buf, size_t len, int *reset);

    /* A sample of the live context during the timeline (cheap, read-only). */
    void (*sample_context)(void *u, ex_ctx_sample *out);

    /* The title ID of a runtime app id the system itself reported as the running big app. */
    void (*query_title)(void *u, int32_t app_id, ex_title_query *out);

    /* CPU and peak memory of this process. */
    void (*read_usage)(void *u, ex_usage *out);
} ex_backend;

void ex_backend_host(ex_backend *be, void *sim);
#ifdef AKN_EX_PS5
void ex_backend_ps5(ex_backend *be);
#endif

#endif
