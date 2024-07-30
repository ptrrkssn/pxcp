/*
 * fsobj.c
 *
 * Copyright (c) 2024, Peter Eriksson <pen@lysator.liu.se>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef __linux__
#define _GNU_SOURCE
#endif

#include "config.h"
#include "fsobj.h"

extern int f_debug;

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>

#ifndef AT_RESOLVE_BENEATH
#define AT_RESOLVE_BENEATH 0
#endif


#if !HAVE_FUNLINKAT
#define funlinkat(dirfd,name,fd,flags) unlinkat(dirfd,name,flags)
#endif



void
fsobj_init(FSOBJ *op) {
    if (!op)
	abort();

    memset(op, 0, sizeof(*op));
    op->magic = FSOBJ_MAGIC;
    op->fd = -1;

    if (f_debug > 2)
        fprintf(stderr, "** fsobj_init(%p)\n", op);
}


void
fsobj_reset(FSOBJ *op) {
    /* Sanity check */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
	abort();

    if (f_debug > 2)
        fprintf(stderr, "** fsobj_reset(%p) path=%s @ %d\n", op, fsobj_path(op), op->fd);

    /* Make sure no references to this object */
    if (op->refcnt > 0)
	abort();

    if (op->fd >= 0) {
	close(op->fd);
	op->fd = -1;
    }

    if (op->name) {
	free((void *) op->name);
	op->name = NULL;
    }

    if (op->path) {
	free((void *) op->path);
	op->path = NULL;
    }

    if (op->parent) {
	op->parent->refcnt--;
	op->parent = NULL;
    }

    if (op->dbuf) {
	free(op->dbuf);
	op->dbuf = NULL;
	op->dbufpos = 0;
	op->dbuflen = 0;
        op->dbufsize = 0;
    }

    op->flags = 0;
    memset(&op->stat, 0, sizeof(op->stat));
}


void
fsobj_fini(FSOBJ *op) {
    /* Sanity check */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
	abort();

    if (f_debug > 2)
        fprintf(stderr, "** fsobj_fini(%p)\n", op);

    fsobj_reset(op);
    op->magic = 0;
}



int
fsobj_isopen(const FSOBJ *op) {
    /* Sanity check */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
	abort();

    return (op->fd >= 0);
}


int
fsobj_mkdir(FSOBJ *op,
            mode_t mode) {
    int rc;

    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
        abort();
    if (op->fd >= 0)
        abort();

    if (!op->parent) {
        errno = EBADF;
        return -1;
    }

    if (op->parent->fd < 0) {
        /* Non existing parent, return a non-opened pathref to a directory */
        op->flags = O_PATH;
        op->stat.st_mode = (S_IFDIR|mode);
        return 1;
    }

    rc = mkdirat(op->parent->fd, op->name, mode);
    if (f_debug > 2)
        fprintf(stderr, "** fsobj_mkdir: mkdirat(%p, %s) @ %d -> %d (%s)\n",
                op, fsobj_path(op), op->fd, rc, strerror(errno));
    if (rc < 0 && errno != EEXIST)
        return -1;

    op->fd = openat(op->parent->fd, op->name, O_RDONLY|O_NOFOLLOW|O_DIRECTORY);
    if (op->fd < 0)
        return -1;

#ifdef HAVE_FSTATAT
#ifdef AT_EMPTY_PATH
    rc = fstatat(op->fd, "", &op->stat, AT_EMPTY_PATH);
#else
    rc = fstatat(op->parent->fd, op->name, &op->stat, AT_SYMLINK_NOFOLLOW);
#endif
#else
    rc = fstat(op->fd, &op->stat);
#endif
    if (rc < 0) {
        int s_errno = errno;
        close(op->fd);
        op->fd = -1;
        errno = s_errno;
        return -1;
    }

    op->flags = O_RDONLY;
    return (rc < 0 ? 0 : 1);
}

/*
 * Open a filesystem object.
 *
 * IF O_PATH in flags
 *   IF object type selected in mode
 *     IF object not found
 *       THEN
 *         create a closed reference with faked object type in stat
 *       return 0
 *     ELSE
 *       IF object type NOT same as selected
 *       THEN
 *         return -1
 *       return 1
 * ELSE
 * Return values:
 *   >0 Object type
 *    0 Not found
 *   <0 Error
 */
int
fsobj_open(FSOBJ *op,
	   FSOBJ *parent,
	   const char *name,
	   int flags,
	   mode_t mode) {
    struct stat sb;
    int fd, pfd;


    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
        abort();

    if (f_debug > 2)
        fprintf(stderr, "** fsobj_open(%p)\n", op);

    if ((flags & (O_ACCMODE|O_PATH)) == O_PATH) {
        /* Open a path reference, and possibly open the object file descriptor */

        pfd = (parent ? (parent->fd >= 0 ? parent->fd : -1) : AT_FDCWD);
        if (pfd != -1)
            /* Valid parent */
            fd = openat(pfd, name, flags|O_NOFOLLOW|(parent ? AT_RESOLVE_BENEATH : 0));
        else {
            /* Parent does not exist - Generate a non-opened fsobj reference */
            errno = ENOENT;
            fd = -1;
        }

        if (f_debug > 2)
            fprintf(stderr, "** fsobj_open(%s%s%s) -> %d/%d (%s) [O_PATH]\n",
                    parent ? parent->name : "",
                    parent ? "/" : "",
                    name,
                    pfd, fd,
                    fd < 0 ? strerror(errno) : "");

        if (fd < 0 && errno == ENOENT && (mode&S_IFMT) != 0) {
            /* Object does not exist, generate a non-opened path reference object */
            memset(&sb, 0, sizeof(sb));
            sb.st_mode = mode;
            goto Create;
        }
    } else {

        pfd = (parent && parent->fd >= 0 ? parent->fd : AT_FDCWD);

        if (flags & O_CREAT) {
            if ((mode & S_IFMT) == S_IFDIR) {
                /* Create a new directory, ok if it already exists */
                if (mkdirat(pfd, name, mode&ALLPERMS) < 0 && errno != EEXIST) {
                    if (f_debug > 2)
                        fprintf(stderr, "** fsobj_open: mkdirat(%s%s%s) @ %d -> %s\n",
                                parent ? parent->name : "",
                                parent ? "/" : "",
                                name, pfd, strerror(errno));
                    return -1;
                }

                flags &= ~O_CREAT;
                fd = openat(pfd, name, flags|O_NOFOLLOW, mode&ALLPERMS);
                if (f_debug > 2)
                    fprintf(stderr, "** fsobj_open(%s%s%s) @ %d/%d -> (%s) [S_IFDIR & O_CREAT]\n",
                            parent ? parent->name : "",
                            parent ? "/" : "",
                            name, pfd, fd,
                            fd < 0 ? strerror(errno) : "");
            } else {
                fd = openat(pfd, name, flags|O_NOFOLLOW, mode&ALLPERMS);
                if (f_debug > 2)
                    fprintf(stderr, "** fsobj_open(%s%s%s) @ %d/%d -> (%s) [S_IFREG & O_CREAT]\n",
                            parent ? parent->name : "",
                            parent ? "/" : "",
                            name, pfd, fd, strerror(errno));
            }

        } else {
            fd = openat(pfd, name, flags|O_NOFOLLOW);
            if (f_debug > 2)
                fprintf(stderr, "** fsobj_open(%s%s%s) @ %d/%d -> (%s) [!O_CREAT]\n",
                        parent ? parent->name : "",
                        parent ? "/" : "",
                        name, pfd, fd,
                        fd < 0 ? strerror(errno) : "");
        }
    }

    if (fd < 0)
        return -1;

    if (fstatat(fd, "", &sb, AT_EMPTY_PATH) < 0) {
        int s_errno = errno;

	close(fd);
        errno = s_errno;
	return -1;
    }


 Create:
    fsobj_reset(op);

    op->parent = parent;
    if (parent)
	parent->refcnt++;

    op->flags = flags;
    op->stat = sb;
    op->name = strdup(name);
    op->fd = fd;

    return (sb.st_mode&S_IFMT);
}

int
fsobj_fake(FSOBJ *dst,
	   FSOBJ *dstdir,
	   const FSOBJ *src) {
    fsobj_reset(dst);

    dst->flags = O_PATH;
    dst->name = strdup(src->name);
    dst->stat = src->stat;
    time(&dst->stat.st_mtime);
    time(&dst->stat.st_ctime);
    time(&dst->stat.st_atime);
    dst->stat.st_dev = 0;
    dst->stat.st_ino = 0;
    dst->parent = dstdir;
    dst->parent->refcnt++;

    return (dst->stat.st_mode & S_IFMT);
}

int
fsobj_typeof(const FSOBJ *op) {
    /* Sanity check */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
	abort();

    return (op->stat.st_mode & S_IFMT);
}

int
fsobj_equal(const FSOBJ *a,
	    const FSOBJ *b) {
    if (!fsobj_isopen(a) || !fsobj_isopen(b))
	return -1;

    return ((a->stat.st_dev == b->stat.st_dev) &&
	    (a->stat.st_ino == b->stat.st_ino));
}


char *
fsobj_path(FSOBJ *op) {
    size_t blen = 0;
    FSOBJ *tp;
    int rc;


    rc = fsobj_isopen(op);
    if (rc < 0)
	return NULL;

    if (!op->name)
	return NULL;

    if (op->path)
	return op->path;

    tp = op;
    for (tp = op; tp; tp = tp->parent) {
	blen += strlen(tp->name)+1;
    }

    op->path = malloc(blen);
    if (!op->path)
	return NULL;
    op->path[--blen] = '\0';

    for (tp = op; tp; tp = tp->parent) {
	size_t slen = strlen(tp->name);

	if (tp != op)
	    op->path[--blen] = '/';

	memcpy(op->path+blen-slen, tp->name, slen);
	blen -= slen;
    }

    return op->path;
}


/*
 * Refresh stat information for an object
 *
 * Return values:
 *  -1 an error occured
 *   0 stat update
 *   1 object was removed, fsobj changed to unopened pathref
 *   2 object was replaced, fsobj changed to unopened pathref
 */
int
fsobj_refresh(FSOBJ *op) {
    struct stat sb;
    int pfd, rc;

    /* Sanity checks */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
        abort();

    memset(&sb, 0, sizeof(sb));
    pfd = op->parent && op->parent->fd >= 0 ? op->parent->fd : AT_FDCWD;

    rc = fstatat(pfd, op->name, &sb, AT_SYMLINK_NOFOLLOW|AT_RESOLVE_BENEATH);
    if (rc < 0 && errno != ENOENT)
        return -1;

    if (rc < 0 || sb.st_ino != op->stat.st_ino || sb.st_dev != op->stat.st_dev) {
        /* Object went away, or was replaced */

        if (op->fd >= 0)
            close(op->fd);
        op->fd = -1;

        op->flags = O_PATH;

        /* Retain object type information */
        sb.st_mode = (op->stat.st_mode&S_IFMT);
        op->stat = sb;

        return (rc < 0 ? 1 : 2);
    }

    op->stat = sb;
    return 0;
}

/*
 * Change object open modes
 */
int
fsobj_reopen(FSOBJ *op,
	     int flags) {
    int nfd;


    /* Sanity check */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
	abort();

    /* Already open with the right flags? */
    if (op->flags == flags)
        return 0;
    
    if (op->parent && op->parent->fd != -1)
        nfd = openat(op->parent->fd, op->name, (flags&~O_CREAT));
    else {
	nfd = -1;
	errno = EBADF;
    }

    if (nfd < 0)
	return -1;

    if (fstatat(nfd, "", &op->stat, AT_EMPTY_PATH) < 0) {
	close(nfd);
	return -1;
    }

    if (dup2(nfd, op->fd) < 0) {
	close(nfd);
	return -1;
    }

    op->flags = flags;

    if (close(nfd) < 0)
	return -1;

    return 0;
}

void
fsobj_close(FSOBJ *op) {
    /* Sanity checks */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
        abort();

    if (f_debug > 2)
        fprintf(stderr, "** fsobj_close(%s) @ %d\n", fsobj_path(op), op->fd);

    if (op->fd >= 0) {
	close(op->fd);
	op->fd = -1;
    }

    op->fdpos = 0;
    op->flags = O_PATH;
}

int
fsobj_delete(FSOBJ *op) {
    int rc = fsobj_isopen(op);
    if (rc < 0)
	return -1;
    else if (rc == 0) {
	return 0;
    }

    if (funlinkat(op->parent->fd, op->name, op->fd,
		  AT_RESOLVE_BENEATH|(S_ISDIR(op->stat.st_mode) ? AT_REMOVEDIR : 0)) < 0)
	return -1;

    fsobj_close(op);

    return 0;
}


int
fsobj_rewind(FSOBJ *op) {
    int rc = fsobj_isopen(op);

    if (rc <= 0)
	return rc;

    op->fdpos = 0;
    return lseek(op->fd, op->fdpos, SEEK_SET);
}

#define DIRBUFSIZE ((sizeof(struct dirent)+MAXNAMLEN+1)*256)

int
fsobj_readdir(FSOBJ *dp, FSOBJ *op) {
    ssize_t rc;
    size_t bufsize = DIRBUFSIZE;
    struct dirent *dep;


    /* Sanity checks */
    if (!dp)
        abort();
    if (dp->magic != FSOBJ_MAGIC)
        abort();
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
        abort();

    /* Probably won't happen, but you never know */
    if (dp->stat.st_blksize > bufsize)
	bufsize = dp->stat.st_blksize;

    if (!dp->dbuf) {
	dp->dbuf = malloc(bufsize);
	if (!dp->dbuf)
            abort();

        dp->dbufsize = bufsize;
	dp->dbufpos = 0;
	dp->dbuflen = 0;
    } else {
        /* Probably won't happen but.. */
        if (bufsize > dp->dbufsize) {
            dp->dbuf = realloc(dp->dbuf, bufsize);
            if (!dp->dbuf)
                abort();
            dp->dbufsize = bufsize;
        }
    }

 Again:
    if (dp->dbufpos >= dp->dbuflen) {
	dp->dbufpos = 0;

	rc = getdirentries(dp->fd, dp->dbuf, dp->dbufsize, &dp->fdpos);
	if (rc <= 0) {
	    dp->dbuflen = 0;
	    return rc;
	}

	dp->dbuflen = rc;
    }

    dep = (struct dirent *) (dp->dbuf+dp->dbufpos);
    dp->dbufpos += dep->d_reclen;

    if (dep->d_name[0] == '.' && (dep->d_name[1] == '\0' || (dep->d_name[1] == '.' && dep->d_name[2] == '\0')))
	goto Again;

    fsobj_reset(op);
    rc = fsobj_open(op, dp, dep->d_name, O_PATH, 0);
    if (rc == 0) {
        /* Object disappeared while trying to open it, skip it */
        goto Again;
    }

    return rc > 0 ? 1 : -1;
}


int
fsobj_rename(FSOBJ *op,
	     char *name) {
    int rc = 0;


    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
	abort();
    if (op->name == NULL)
        abort();

    if (op->fd >= 0 && op->parent && op->parent->fd >= 0) {
        if (renameat(op->parent->fd, op->name,
                     op->parent->fd, name) < 0) {
            return -1;
        }
    }

    /* Update stat information for new object */
    fsobj_refresh(op);

    free(op->name);
    op->name = strdup(name);
    if (!op->name)
	abort();

    return rc;
}
