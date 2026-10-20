/* The Audio Lab's control-file handling (al_control.h). Portable; unit-tested against real files. */
#define _POSIX_C_SOURCE 200809L
#include "al_control.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

void al_control_begin(const char *dir) {
    char path[320], aside[340];
    snprintf(path, sizeof(path), "%s/control", dir);
    struct stat st;
    if (stat(path, &st) != 0) return; /* nothing to move aside */
    snprintf(aside, sizeof(aside), "%s/control.consumed", dir);
    if (rename(path, aside) != 0) unlink(path); /* at worst remove it, but never read it as commands */
}

int al_control_read(const char *dir, long *offset, char *buf, size_t len, int *reset) {
    char path[320];
    snprintf(path, sizeof(path), "%s/control", dir);
    *reset = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size < *offset) {
        *offset = 0; /* the file was replaced or truncated: read the new content from the start */
        *reset = 1;
    }
    if (lseek(fd, *offset, SEEK_SET) == (off_t)-1) {
        close(fd);
        return 0;
    }
    ssize_t n = read(fd, buf, len);
    if (n > 0) *offset += (long)n;
    close(fd);
    return n > 0 ? (int)n : 0;
}
