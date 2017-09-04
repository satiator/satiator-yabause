/*  Copyright 2015 James Laird-Wah

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*! \file fffake.c
    \brief HLE of FatFS access to files & directories
*/

#define DIR UNIX_DIR
#include <dirent.h>
#undef DIR
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../satisfier.h"
#include "ff.h"

static int cwd_fd;
static char *cwd;
static char *root_path;

#define FD (*(int*)fp)
#define FD_GOOD (FD >= 0)

#define file_exists(path) (access(path, F_OK) == 0)

FRESULT errno_status(void) {
    switch (errno) {
        case 0:
            return FR_OK;
        case EACCES:
            return FR_DENIED;
        case EISDIR:
        case ENOENT:
            return FR_NO_FILE;
        case ENOTDIR:
            return FR_NO_PATH;
        default:
            perror("unhandled error");
            return FR_DISK_ERR;
    }
}

FRESULT f_open (FIL* fp, const TCHAR* path, BYTE mode) {
    if (file_exists(path) && (mode & FA_CREATE_NEW))
        return FR_EXIST;
    if (!file_exists(path) && (mode & FA_OPEN_EXISTING))
        return FR_NO_FILE;

    int flags = 0;
    if (mode & FA_WRITE)
        flags = O_WRONLY;
    if (mode & FA_READ)
        flags = O_RDONLY;
    if (mode & FA_READ &&
        mode & FA_WRITE)
        flags = O_RDWR;

    if (mode & FA_CREATE_ALWAYS)
        flags |= O_TRUNC;
    if (mode & (FA_OPEN_ALWAYS | FA_CREATE_NEW | FA_CREATE_ALWAYS))
        flags |= O_CREAT;

    errno = 0;
    int fd = openat(cwd_fd, path, flags, 0666);
    FD = fd;

    if (FD_GOOD) {
        fp->fsize = lseek(FD, 0, SEEK_END);
        fp->fptr = lseek(FD, 0, SEEK_SET);
        return FR_OK;
    } else {
        perror("openat");
        printf("Couldn't open '%s'\n", path);
        return errno_status();
    }
}
FRESULT f_close (FIL* fp) {
    if (!FD_GOOD)
        return FR_INVALID_OBJECT;

    if (FD == 0)    // uninit mem. bad!
        abort();

    errno = 0;
    close(FD);
    return errno_status();
}
FRESULT f_read (FIL* fp, void* buff, UINT btr, UINT* br) {
    if (!FD_GOOD)
        return FR_INVALID_OBJECT;

    *br = read(FD, buff, btr);

    if (*br < 0) {
        perror("read");
        return FR_DISK_ERR;
    }

    fp->fptr += *br;

    return FR_OK;
}
FRESULT f_write (FIL* fp, const void* buff, UINT btw, UINT* bw) {
    if (!FD_GOOD)
        return FR_INVALID_OBJECT;

    *bw = write(FD, buff, btw);
    fp->fptr += *bw;    // size may need updating too
    return FR_OK;
}
FRESULT f_lseek (FIL* fp, DWORD ofs) {
    if (!FD_GOOD)
        return FR_INVALID_OBJECT;
    fp->fptr = lseek(FD, ofs, SEEK_SET);
    return FR_OK;
}
FRESULT f_truncate (FIL* fp) {
    if (!FD_GOOD)
        return FR_INVALID_OBJECT;
    ftruncate(FD, fp->fptr);
    fp->fsize = fp->fptr;
    return FR_OK;
}
FRESULT f_sync (FIL* fp) {
    if (!FD_GOOD)
        return FR_INVALID_OBJECT;
    fsync(FD);
    return FR_OK;
}

#define DIRPTR (*(UNIX_DIR**)dp)
FRESULT f_opendir (DIR* dp, const TCHAR* path) {
    errno = 0;
    int fd = openat(cwd_fd, path, O_RDONLY|O_DIRECTORY);
    DIRPTR = fdopendir(fd);
    if (DIRPTR)
        return FR_OK;
    else
        return errno_status();
}
FRESULT f_closedir (DIR* dp) {
    if (!DIRPTR)
        return FR_INVALID_OBJECT;
    closedir(DIRPTR);
    DIRPTR = NULL;
    return F_OK;
}
FRESULT f_readdir (DIR* dp, FILINFO* fno) {
    if (!DIRPTR)
        return FR_INVALID_OBJECT;
    if (!fno) {
        rewinddir(DIRPTR);
    } else {
        struct dirent *d = readdir(DIRPTR);

        // Don't return .. in the root
        if (d && !strcmp(cwd, root_path) && !strcmp(d->d_name, ".."))
            return f_readdir(dp, fno);

        if (d) {
            f_stat(d->d_name, fno);
        } else {
            memset(fno->fname, 0, 13);
            if (fno->lfname)
                fno->lfname[0] = 0;
        }
    }
    return F_OK;
}
FRESULT f_mkdir (const TCHAR* path) {
    errno = 0;
    if (!mkdirat(cwd_fd, path, 0777))
        return F_OK;
    return errno_status();
}
FRESULT f_unlink (const TCHAR* path) {
    errno = 0;
    if (!unlinkat(cwd_fd, path, 0))
        return F_OK;
    return errno_status();
}
FRESULT f_rename (const TCHAR* path_old, const TCHAR* path_new) {
    errno = 0;
    if (!renameat(cwd_fd, path_old, cwd_fd, path_new))
        return F_OK;
    return errno_status();
}
FRESULT f_stat (const TCHAR* path, FILINFO* fno) {
    struct stat st;
    errno = 0;
    if (fstatat(cwd_fd, path, &st, 0))
        return errno_status();

    fno->fsize = st.st_size;    // will overflow if >4gb
    fno->fattrib = 0;
    memcpy(fno->fname, path, 13);  // this does not do 8.3 names!
    if (fno->lfname) {
        strncpy(fno->lfname, path, fno->lfsize);
    }
    if (st.st_mode & S_IFDIR)
        fno->fattrib |= AM_DIR;
    // TODO: dates
    return FR_OK;
}
FRESULT f_mount (FATFS* fs, const TCHAR* path, BYTE opt) {
    cwd_fd = open(path, O_RDONLY | O_DIRECTORY);
    root_path = strdup(path);
    cwd = strdup(path);
    if (cwd_fd < 0) {
        perror("open");
        return FR_NOT_READY;
    } else {
        return FR_OK;
    }
}
FRESULT f_chdir(const TCHAR* path) {
    char newwd[PATH_MAX+1];
    char *newpath;
    if (path[0] == "/")
        asprintf(&newpath, "%s/%s", root_path, path);
    else
        asprintf(&newpath, "%s/%s", cwd, path);
    realpath(newpath, newwd);
    free(newpath);

    if (strncmp(root_path, newwd, strlen(root_path)))   // don't allow traversal past the root
        return FR_INVALID_PARAMETER;

    int fd = open(newwd, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        perror("open");
        return errno_status();
    } else {
        close(cwd_fd);
        cwd_fd = fd;
        free(cwd);
        cwd = strdup(newwd);
        return FR_OK;
    }
}

FRESULT f_getcwd(TCHAR* path, UINT len) {
    snprintf(path, len, "%s/", cwd + strlen(root_path));
}

FRESULT f_mkfs (const TCHAR* path, BYTE sfd, UINT au) {
    // bit dangerous... maybe skip this for now
    return FR_OK;
}
