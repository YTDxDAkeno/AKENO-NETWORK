/*
 * The Explorer's console functions. This is the ONLY file that declares or calls PS5 system
 * functions, and every one is read-only and has run on firmware 12.20 in an AKENO build before (or
 * is a standard FreeBSD/POSIX call the pinned SDK provides):
 *
 *   sceKernelSendNotificationRequest   the daemon, AKENO Native, the Voice Lab (samples/hello_world)
 *   kernel_get_fw_version              SDK runtime, read only (daemon, AKENO Native)
 *   sysctl(KERN_PROC_PID)              this process only; standard FreeBSD, read only (samples/ps)
 *   sceKernelGetAppInfo                weak, read only, for getpid() (AKENO Native's context check)
 *   sceSystemServiceGetAppIdOfRunningBigApp   weak, read only (AKENO Native; websrv hbldr.c)
 *   sceUserServiceInitialize / GetForegroundUser   the daemon; weak (AKENO Native)
 *   getrusage / getrlimit              standard POSIX, this process only (SDK libc)
 *   dlopen / dlsym / dlclose           samples/hello_dlfcn; look-up only, addresses never called;
 *                                      only the libraries the catalogue marks loadable (quarantined
 *                                      ones are never passed here)
 *   stat                               read-only file presence under /system/common/lib
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
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
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
int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(uint32_t *user_id) __attribute__((weak));

#define APP_INFO_BYTES 512

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

static void read_own_proc(ex_raw_proc *out) {
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    struct kinfo_proc ki;
    size_t size = sizeof(ki);
    if (sysctl(mib, 4, &ki, &size, NULL, 0) != 0 || size < sizeof(ki)) return;
    out->have_proc = 1;
    out->ppid = ki.ki_ppid;
    snprintf(out->comm, sizeof(out->comm), "%.*s", (int)sizeof(ki.ki_comm), ki.ki_comm);
    out->proc_stat = ki.ki_stat;
    out->start_unix = (long long)ki.ki_start.tv_sec;
    long page = getpagesize();
    out->rss_bytes = page > 0 ? (long long)ki.ki_rssize * page : -1;
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

/* Reads bytes appended to the control file since the last call (no network). */
static int p_read_control(void *u, char *buf, size_t len) {
    (void)u;
    static off_t offset = 0;
    static int missing_logged = 0;
    int fd = open("/data/akeno-system-explorer/control", O_RDONLY);
    if (fd < 0) {
        missing_logged = 1;
        return 0;
    }
    (void)missing_logged;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        close(fd);
        return 0;
    }
    ssize_t n = read(fd, buf, len);
    if (n > 0) offset += n;
    close(fd);
    return n > 0 ? (int)n : 0;
}

static void p_sample_context(void *u, int32_t *bigapp, int32_t *user) {
    (void)u;
    *bigapp = sceSystemServiceGetAppIdOfRunningBigApp ? sceSystemServiceGetAppIdOfRunningBigApp() : -1;
    if (sceUserServiceGetForegroundUser) {
        uint32_t id = 0;
        if (sceUserServiceGetForegroundUser(&id) == 0) *user = (int32_t)id;
    }
}

void ex_backend_ps5(ex_backend *be) {
    (void)sceUserServiceInitialize(NULL); /* required before GetForegroundUser (as the daemon does) */
    ex_backend b = { "PlayStation 5", NULL, p_now_us, p_sleep_ms, p_unix, p_notify, p_read_proc,
                     p_platform, p_file_present, p_lib_lookup, p_read_control, p_sample_context };
    *be = b;
}
