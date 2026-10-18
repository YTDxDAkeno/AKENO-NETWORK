#define _POSIX_C_SOURCE 200809L
#include "update_fs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

int akn_fs_write_atomic(const char *path, const void *data, size_t len) {
    char tmp[512];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    const unsigned char *p = data;
    size_t left = len;
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            int e = errno;
            close(fd);
            unlink(tmp);
            errno = e;
            return -1;
        }
        p += n;
        left -= (size_t)n;
    }
    if (fsync(fd) != 0 || close(fd) != 0) {
        int e = errno;
        unlink(tmp);
        errno = e;
        return -1;
    }
    if (rename(tmp, path) != 0) {
        int e = errno;
        unlink(tmp);
        errno = e;
        return -1;
    }
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        akn_fs_sync_dir(dir);
    }
    return 0;
}

char *akn_fs_read_file(const char *path, size_t max, size_t *len) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (unsigned long long)st.st_size > max) {
        close(fd);
        return NULL;
    }
    size_t size = (size_t)st.st_size;
    char *buf = malloc(size + 1);
    if (!buf) {
        close(fd);
        return NULL;
    }
    size_t got = 0;
    while (got < size) {
        ssize_t n = read(fd, buf + got, size - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    if (got != size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    if (len) *len = size;
    return buf;
}

int akn_fs_mkdirs(const char *dir) {
    char path[512];
    if (snprintf(path, sizeof(path), "%s", dir) >= (int)sizeof(path)) return -1;
    for (char *p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(path, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) return -1;
    return akn_fs_is_dir(path) ? 0 : -1;
}

static int remove_tree(const char *dir, int depth) {
    if (depth > 8) return -1;
    struct stat st;
    if (lstat(dir, &st) != 0) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode)) return unlink(dir);
    DIR *d = opendir(dir);
    if (!d) return -1;
    int rc = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char child[512];
        if (snprintf(child, sizeof(child), "%s/%s", dir, e->d_name) >= (int)sizeof(child)) {
            rc = -1;
            continue;
        }
        if (remove_tree(child, depth + 1) != 0) rc = -1;
    }
    closedir(d);
    if (rmdir(dir) != 0) rc = -1;
    return rc;
}

int akn_fs_remove_tree(const char *dir) { return remove_tree(dir, 0); }

int akn_fs_is_dir(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int akn_fs_is_file(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
}

void akn_fs_sync_dir(const char *dir) {
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return;
    (void)fsync(fd);
    close(fd);
}

long long akn_fs_free_bytes(const char *path) {
    struct statvfs sv;
    if (statvfs(path, &sv) != 0) return -1;
    return (long long)sv.f_bavail * (long long)sv.f_frsize;
}

const char *akn_fs_error_text(int err) {
    switch (err) {
    case ENOSPC: return "There is not enough free space on the console's storage.";
    case EACCES:
    case EPERM: return "AKENO NETWORK is not allowed to write to its folder.";
    case EROFS: return "The storage is read-only.";
    case ENOENT: return "A folder of AKENO NETWORK is missing.";
    case EIO: return "The storage reported a read or write error.";
    default: return NULL;
    }
}
