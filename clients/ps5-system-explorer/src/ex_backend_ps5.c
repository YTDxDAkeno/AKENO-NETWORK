/*
 * The Explorer's console functions (6.1). This is the ONLY file that declares or calls PS5 system
 * functions, and every one is read-only and has run on firmware 12.20 in an AKENO build before (or
 * is a standard FreeBSD/POSIX call the pinned SDK provides):
 *
 *   sceKernelSendNotificationRequest   the daemon, AKENO Native, the Voice Lab (samples/hello_world)
 *   kernel_get_fw_version              SDK runtime, read only (daemon, AKENO Native)
 *   sysctl(KERN_PROC_PID)              this process only; standard FreeBSD, read only (samples/ps)
 *   sceKernelGetAppInfo                weak, read only, for getpid() (AKENO Native's context check)
 *   sceSystemServiceGetAppIdOfRunningBigApp   weak, read only (AKENO Native; websrv hbldr.c)
 *   sceSystemServiceGetAppTitleId      weak, read only (6.1): declared int(int app_id, char*) by
 *                                      onionHEN util_platform.c, etaHEN commands.cpp, LiveShell
 *                                      liveshell_daemon.cpp; called only for an app id the system
 *                                      just reported as the running big app (see query_title)
 *   sceUserServiceInitialize / GetForegroundUser   the daemon; weak (AKENO Native)
 *   getrusage / getrlimit              standard POSIX, this process only (SDK libc)
 *   dlopen / dlsym / dlclose           samples/hello_dlfcn; look-up only, addresses never called;
 *                                      only the libraries the catalogue marks loadable (quarantined
 *                                      ones are never passed here)
 *   stat / flock / open / rename       read-only file presence, advisory lock, atomic report write
 *
 * Not used, by rule (CI: console/check-explorer.sh): no app launch/kill, mount, ptrace, kernel
 * memory helpers, pad, registry, install util, audio or microphone call. The Explorer opens no
 * audio port and no microphone in this build.
 */
/* No _POSIX_C_SOURCE here: <sys/proc.h>/<sys/user.h> need the BSD types (u_int, ...), exactly as
 * the AKENO daemon's platform_ps5.c includes them. */
#include "ex_backend.h"
#include "ex.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

typedef struct {
    char reserved[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int blocking);
int sceKernelGetAppInfo(pid_t pid, void *info) __attribute__((weak));
int sceSystemServiceGetAppIdOfRunningBigApp(void) __attribute__((weak));
int sceSystemServiceGetAppTitleId(int app_id, char *title_id) __attribute__((weak));
int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(uint32_t *user_id) __attribute__((weak));

#define APP_INFO_BYTES 512

static ex_errno_kind classify_errno(int e) {
    switch (e) {
    case 0: return EX_ERRNO_NONE;
    case ENOMEM: return EX_ERRNO_NOMEM;
    case EPERM: return EX_ERRNO_PERM;
    case EACCES: return EX_ERRNO_ACCES;
    case ESRCH: return EX_ERRNO_SRCH;
    case EINVAL: return EX_ERRNO_INVAL;
    case ENOENT: return EX_ERRNO_NOENT;
#ifdef EOPNOTSUPP
    case EOPNOTSUPP: return EX_ERRNO_OPNOTSUPP;
#endif
#if defined(ENOTSUP) && (!defined(EOPNOTSUPP) || ENOTSUP != EOPNOTSUPP)
    case ENOTSUP: return EX_ERRNO_OPNOTSUPP;
#endif
    case ENOSYS: return EX_ERRNO_OPNOTSUPP;
    case EFAULT: return EX_ERRNO_FAULT;
    default: return EX_ERRNO_OTHER;
    }
}

static uint64_t p_now_us(void *u) {
    (void)u;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void p_sleep_ms(void *u, unsigned ms) {
    (void)u;
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static long long p_unix(void *u) {
    (void)u;
    return (long long)time(NULL);
}

static int p_notify(void *u, const char *text) {
    (void)u;
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.message, sizeof(req.message), "%s", text);
    return sceKernelSendNotificationRequest(0, &req, sizeof(req), 0) == 0 ? 0 : -1;
}

static const char *p_platform(void *u) {
    (void)u;
    return "PlayStation 5";
}

/*
 * The per-process query, in two steps with errno captured after each, so a failure on FW 12.20 is
 * diagnosable. The buffer is a single kinfo_proc (this is KERN_PROC_PID, which returns at most one
 * record). The record is decoded only when the kernel's own ki_structsize equals the SDK's
 * sizeof(struct kinfo_proc) and ki_pid is this PID - a different firmware layout is never guessed.
 */
static void read_own_proc(ex_raw_proc *out) {
    ex_proc_query *q = &out->pq;
    memset(q, 0, sizeof(*q));
    q->attempted = 1;
    q->sdk_size = sizeof(struct kinfo_proc);

    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    size_t size = 0;
    errno = 0;
    int rc = sysctl(mib, 4, NULL, &size, NULL, 0);
    q->size_rc = rc;
    if (rc != 0) {
        q->size_errno = errno;
        q->size_errno_kind = classify_errno(errno);
        return;
    }
    q->size_reported = size;

    struct kinfo_proc ki;
    memset(&ki, 0, sizeof(ki));
    size_t want = sizeof(ki);
    q->requested = want;
    q->buffer_bound = sizeof(ki);
    if (q->size_reported > q->buffer_bound) return; /* RECORD_TOO_LARGE; not read into a too-small buffer */
    errno = 0;
    rc = sysctl(mib, 4, &ki, &want, NULL, 0);
    q->read_rc = rc;
    if (rc != 0) {
        q->read_errno = errno;
        q->read_errno_kind = classify_errno(errno);
        return;
    }
    q->returned = want;
    if (want >= sizeof(int)) {
        q->have_structsize = 1;
        q->structsize = ki.ki_structsize;
    }
    /* ki_pid lives at the same offset in our SDK's struct; compare only when the size matches. */
    if (want == sizeof(ki)) {
        q->have_pid_at_sdk_offset = 1;
        q->pid_at_sdk_offset = ki.ki_pid;
    }
    if (q->have_structsize && (size_t)q->structsize == sizeof(ki) && want == sizeof(ki) &&
        q->pid_at_sdk_offset == getpid()) {
        q->decoded = 1;
        out->have_proc = 1;
        out->ppid = ki.ki_ppid;
        snprintf(out->comm, sizeof(out->comm), "%.*s", (int)sizeof(ki.ki_comm), ki.ki_comm);
        out->proc_stat = ki.ki_stat;
        out->start_unix = (long long)ki.ki_start.tv_sec;
        long page = getpagesize();
        out->rss_bytes = page > 0 ? (long long)ki.ki_rssize * page : -1;
    }
}

static void p_read_proc(void *u, ex_raw_proc *out) {
    (void)u;
    memset(out, 0, sizeof(*out));
    uint32_t fw = kernel_get_fw_version();
    out->have_fw = fw != 0;
    out->firmware_raw = fw;
    out->pid = getpid();
    out->ppid = getppid();

    read_own_proc(out);
    if (!out->have_proc) out->rss_bytes = -1;

    out->have_appinfo_fn = sceKernelGetAppInfo != NULL;
    if (out->have_appinfo_fn) {
        unsigned char info[APP_INFO_BYTES];
        memset(info, 0, sizeof(info));
        out->appinfo_rc = sceKernelGetAppInfo(getpid(), info);
        memcpy(out->appinfo_raw, info, sizeof(out->appinfo_raw));
    }
    out->have_bigapp_fn = sceSystemServiceGetAppIdOfRunningBigApp != NULL;
    if (out->have_bigapp_fn) out->bigapp_app_id = sceSystemServiceGetAppIdOfRunningBigApp();

    out->have_user_fn = sceUserServiceGetForegroundUser != NULL;
    if (out->have_user_fn) {
        uint32_t user = 0;
        out->user_rc = sceUserServiceGetForegroundUser(&user);
        if (out->user_rc == 0) out->foreground_user = user;
    }

    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        out->have_rlimit = 1;
        out->rlimit_nofile_cur = rl.rlim_cur == RLIM_INFINITY ? -1 : (long long)rl.rlim_cur;
        out->rlimit_nofile_max = rl.rlim_max == RLIM_INFINITY ? -1 : (long long)rl.rlim_max;
    }
    if (getrlimit(RLIMIT_AS, &rl) == 0) {
        out->have_rlimit = 1;
        out->rlimit_as_cur = rl.rlim_cur == RLIM_INFINITY ? -1 : (long long)rl.rlim_cur;
    } else {
        out->rlimit_as_cur = -1;
    }

    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        out->have_rusage = 1;
        out->cpu_user_s = (double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec / 1e6;
        out->cpu_sys_s = (double)ru.ru_stime.tv_sec + (double)ru.ru_stime.tv_usec / 1e6;
        out->maxrss_bytes = (long long)ru.ru_maxrss * 1024; /* ru_maxrss is in KiB */
    }
}

static int p_file_present(void *u, const char *path) {
    (void)u;
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int p_lib_lookup(void *u, const char *library, const char *const *names, int *found, size_t count,
                        char *note, size_t note_len) {
    (void)u;
    for (size_t i = 0; i < count; i++) found[i] = 0;
    void *lib = dlopen(library, RTLD_LAZY);
    if (!lib) {
        const char *why = dlerror();
        snprintf(note, note_len, "dlopen failed: %s", why ? why : "unknown");
        return -1;
    }
    size_t present = 0;
    for (size_t i = 0; i < count; i++) {
        found[i] = dlsym(lib, names[i]) != NULL; /* looked up only; never called */
        present += found[i] != 0;
    }
    dlclose(lib);
    snprintf(note, note_len, "loaded, %zu of %zu names present", present, count);
    return 0;
}

/*
 * Reads bytes appended to the control file since the last call (no network). 6.1: a shrink of the
 * file (it was replaced or truncated) is reported through *reset and reading restarts at 0, so a
 * stale leftover line from an earlier run cannot be read as a half command.
 */
static int p_read_control(void *u, char *buf, size_t len, int *reset) {
    (void)u;
    static off_t offset = 0;
    *reset = 0;
    int fd = open("/data/akeno-system-explorer/control", O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size < offset) {
        offset = 0;
        *reset = 1;
    }
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        close(fd);
        return 0;
    }
    ssize_t n = read(fd, buf, len);
    if (n > 0) offset += n;
    close(fd);
    return n > 0 ? (int)n : 0;
}

static void p_sample_context(void *u, ex_ctx_sample *out) {
    (void)u;
    memset(out, 0, sizeof(*out));
    out->have_bigapp_fn = sceSystemServiceGetAppIdOfRunningBigApp != NULL;
    if (out->have_bigapp_fn) out->bigapp = sceSystemServiceGetAppIdOfRunningBigApp();
    out->have_user_fn = sceUserServiceGetForegroundUser != NULL;
    if (out->have_user_fn) {
        uint32_t id = 0;
        out->user_rc = sceUserServiceGetForegroundUser(&id);
        if (out->user_rc == 0) out->user = id;
    }
}

/*
 * sceSystemServiceGetAppTitleId(app_id, buf). The declaration (two arguments, int app_id and a
 * char* output) is agreed across onionHEN, etaHEN and LiveShell, all read on real consoles; the
 * buffers they pass are 14, 32 and 255 bytes. The function takes NO length, so the safety argument
 * is the buffer size: a window far larger than the largest caller's (256 bytes) means the function
 * cannot write past what those callers already safely give it. The guard bytes are a secondary
 * detector, not the guarantee; a detected overrun disables the query for the rest of the run.
 */
#define TITLE_ARENA 4096u
#define TITLE_WINDOW 256u
#define TITLE_CANARY_BYTE 0xC7

static void p_query_title(void *u, int32_t app_id, ex_title_query *out) {
    (void)u;
    memset(out, 0, sizeof(*out));
    out->nul_at = -1;
    out->guard_ok = 1;
    out->have_fn = sceSystemServiceGetAppTitleId != NULL;
    if (!out->have_fn) return;

    static unsigned char arena[TITLE_ARENA];
    memset(arena, TITLE_CANARY_BYTE, sizeof(arena));
    unsigned char *window = arena + 64; /* 64 canary bytes before, the rest after the window */
    memset(window, 0, TITLE_WINDOW);

    out->called = 1;
    out->rc = sceSystemServiceGetAppTitleId((int)app_id, (char *)window);

    /* Guard: every byte outside the window must still be the canary. */
    for (size_t i = 0; i < 64; i++)
        if (arena[i] != TITLE_CANARY_BYTE) out->guard_ok = 0;
    for (size_t i = 64 + TITLE_WINDOW; i < sizeof(arena); i++)
        if (arena[i] != TITLE_CANARY_BYTE) out->guard_ok = 0;

    int nul = -1, written = 0;
    for (size_t i = 0; i < TITLE_WINDOW; i++) {
        if (window[i] != 0) written = (int)i + 1;
        if (window[i] == 0 && nul < 0) nul = (int)i;
    }
    out->nul_at = nul;
    out->written = written;
    size_t keep = nul >= 0 && (size_t)nul < EX_TITLE_RAW_KEEP ? (size_t)nul : EX_TITLE_RAW_KEEP - 1;
    memcpy(out->raw, window, keep);
    out->raw[keep] = '\0';
}

static void p_read_usage(void *u, ex_usage *out) {
    (void)u;
    memset(out, 0, sizeof(*out));
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return;
    out->have = 1;
    out->cpu_user_s = (double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec / 1e6;
    out->cpu_sys_s = (double)ru.ru_stime.tv_sec + (double)ru.ru_stime.tv_usec / 1e6;
    out->maxrss_bytes = (long long)ru.ru_maxrss * 1024;
}

void ex_backend_ps5(ex_backend *be) {
    (void)sceUserServiceInitialize(NULL); /* required before GetForegroundUser (as the daemon does) */
    ex_backend b = { "PlayStation 5", NULL, p_now_us, p_sleep_ms, p_unix, p_notify, p_read_proc,
                     p_platform, p_file_present, p_lib_lookup, p_read_control, p_sample_context,
                     p_query_title, p_read_usage };
    *be = b;
}
