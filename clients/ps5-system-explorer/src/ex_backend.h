/*
 * The Explorer's platform seam. The PS5 implementation (ex_backend_ps5.c) is the only code that
 * declares or calls console system functions; the PC simulation (ex_backend_host.c) models them
 * with failure injection for the tests. Every call here is read-only.
 */
#ifndef AKN_EX_BACKEND_H
#define AKN_EX_BACKEND_H

#include <stddef.h>
#include <stdint.h>

/* Raw process / application answers, filled by the backend and interpreted by Module A. */
typedef struct {
    int have_fw;
    uint32_t firmware_raw;        /* kernel_get_fw_version(), BCD 0xMMmm0000 */

    int pid, ppid;

    int have_proc;                /* a per-process sysctl (own PID) succeeded */
    char comm[32];                /* ki_comm */
    int proc_stat;                /* ki_stat */
    long long start_unix;         /* ki_start seconds */
    long long rss_bytes;          /* ki_rssize * page size, -1 unknown */

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
     * of bytes read into buf (0 if none / not available). */
    int (*read_control)(void *u, char *buf, size_t len);

    /* A sample of the live context during the timeline (cheap, read-only). */
    void (*sample_context)(void *u, int32_t *bigapp_app_id, int32_t *foreground_user);
} ex_backend;

void ex_backend_host(ex_backend *be, void *sim);
#ifdef AKN_EX_PS5
void ex_backend_ps5(ex_backend *be);
#endif

#endif
