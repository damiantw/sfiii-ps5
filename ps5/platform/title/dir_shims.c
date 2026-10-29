// MIT, see LICENSE.
// A PS5 title is refused the system opendir family (it fails with EPERM), which
// std::filesystem::directory_iterator is built on. These replacements read a
// folder with open(O_DIRECTORY) and the kernel's getdents, one 64 KiB block at a
// time (/data reports 64 KiB directories; a smaller buffer reads nothing there).
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

int getdents(int fd, char* buf, int nbytes);

enum { kDirBuf = 65536 };
struct _dirdesc {   // only declared by <dirent.h>; this file owns the layout
    int fd;
    int len, pos;   // bytes of buf filled by the last getdents, and the next entry
    char buf[kDirBuf];
};

DIR* opendir(const char* path) {
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return NULL;
    DIR* d = malloc(sizeof *d);
    if (!d) { close(fd); errno = ENOMEM; return NULL; }
    d->fd = fd, d->len = d->pos = 0;
    return d;
}

struct dirent* readdir(DIR* d) {
    for (;;) {
        if (d->pos >= d->len) {
            int n = getdents(d->fd, d->buf, kDirBuf);
            if (n <= 0) return NULL;   // end (errno unchanged) or error (errno set)
            d->len = n, d->pos = 0;
        }
        struct dirent* e = (struct dirent*)(d->buf + d->pos);
        if (e->d_reclen == 0) { d->pos = d->len; continue; }
        d->pos += e->d_reclen;
        if (e->d_fileno != 0) return e;
    }
}

void rewinddir(DIR* d) { lseek(d->fd, 0, SEEK_SET); d->len = d->pos = 0; }
int dirfd(DIR* d) { return d->fd; }

int closedir(DIR* d) {
    int r = close(d->fd);
    free(d);
    return r;
}
