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
fsobj_init(FSOBJ *obp) {
    if (!obp)
	abort();

    memset(obp, 0, sizeof(*obp));
    obp->magic = FSOBJ_MAGIC;
    obp->fd = -1;
}


void
fsobj_reset(FSOBJ *obp) {
    /* Sanity check */
    if (!obp || obp->magic != FSOBJ_MAGIC)
	abort();

    /* Make sure no references to this object */
    if (obp->refcnt > 0)
	abort();

    if (obp->fd >= 0) {
	close(obp->fd);
	obp->fd = -1;
    }

    if (obp->name) {
	free((void *) obp->name);
	obp->name = NULL;
    }

    if (obp->path) {
	free((void *) obp->path);
	obp->path = NULL;
    }

    if (obp->parent) {
	obp->parent->refcnt--;
	obp->parent = NULL;
    }

    if (obp->dbuf) {
	free(obp->dbuf);
	obp->dbuf = NULL;
	obp->dbufpos = 0;
	obp->dbuflen = 0;
    }

    obp->flags = 0;
    memset(&obp->stat, 0, sizeof(obp->stat));
}


void
fsobj_fini(FSOBJ *obp) {
    fsobj_reset(obp);
    obp->magic = 0;
}



int
fsobj_isopen(const FSOBJ *obp) {
    if (!obp || obp->magic != FSOBJ_MAGIC)
	abort();

    return (obp->fd >= 0);
}

int
fsobj_isfake(const FSOBJ *obp) {
    if (!obp || obp->magic != FSOBJ_MAGIC)
	abort();

    return (obp->fd < 0);
}



/*
 * Open a filesystem object.
 * Returns:
 *   >0 Object type
 *    0 Not found
 *   <0 Error
 */
int
fsobj_open(FSOBJ *obp,
	   FSOBJ *parent,
	   const char *name,
	   int flags,
	   ...) {
    int fd, rc;
    struct stat sb;
    va_list ap;


    rc = fsobj_isopen(obp);
    if (rc < 0)
	return -1;
    else if (rc == 1) {
	errno = EBADF;
	return -1;
    }

    if (flags & O_CREAT) {
	int mode;

	va_start(ap, flags);
	mode = va_arg(ap, int);

	if (flags & O_DIRECTORY) {
	    /* Create a new directory, ok if it already exists */
	    if (mkdirat(parent ? parent->fd : AT_FDCWD, name, mode) < 0 && errno != EEXIST)
		return -1;

            flags &= ~O_CREAT;
	    fd = openat(parent ? parent->fd : AT_FDCWD, name, flags|O_NOFOLLOW|AT_RESOLVE_BENEATH, mode);
	} else
	    fd = openat(parent ? parent->fd : AT_FDCWD, name, flags|O_NOFOLLOW|AT_RESOLVE_BENEATH, mode);
	va_end(ap);
    } else
	fd = openat(parent ? parent->fd : AT_FDCWD, name, flags|O_NOFOLLOW|AT_RESOLVE_BENEATH);

    if (fd < 0) {
	if (errno == ENOENT)
	    return 0;
	return -1;
    }

    if (fstatat(fd, "", &sb, AT_EMPTY_PATH) < 0) {
	close(fd);
	return -1;
    }

    obp->parent = parent;
    if (parent)
	parent->refcnt++;

    obp->fd = fd;
    obp->flags = flags;
    obp->stat = sb;
    obp->name = strdup(name);

    return (sb.st_mode & S_IFMT);
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
fsobj_typeof(const FSOBJ *objp) {
    return (objp->stat.st_mode & S_IFMT);
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
fsobj_path(FSOBJ *obp) {
    size_t blen = 0;
    FSOBJ *tp;
    int rc;


    rc = fsobj_isopen(obp);
    if (rc < 0)
	return NULL;

    if (!obp->name)
	return NULL;

    if (obp->path)
	return obp->path;

    tp = obp;
    for (tp = obp; tp; tp = tp->parent) {
	blen += strlen(tp->name)+1;
    }

    obp->path = malloc(blen);
    if (!obp->path)
	return NULL;
    obp->path[--blen] = '\0';

    for (tp = obp; tp; tp = tp->parent) {
	size_t slen = strlen(tp->name);

	if (tp != obp)
	    obp->path[--blen] = '/';

	memcpy(obp->path+blen-slen, tp->name, slen);
	blen -= slen;
    }

    return obp->path;
}


/*
 * Change object open modes
 */
int
fsobj_reopen(FSOBJ *obp,
	     int flags) {
    int nfd, rc;


    rc = fsobj_isopen(obp);
    if (rc < 0)
	return -1;

#ifdef O_EMPTY_PATH
    if (obp->fd == -1)
	nfd = openat(obp->fd, "", O_EMPTY_PATH|flags);
    else
#endif
      if (obp->parent && obp->parent->fd != -1)
	nfd = openat(obp->parent->fd, obp->name, flags);
    else {
	nfd = -1;
	errno = EBADF;
    }

    if (nfd < 0)
	return -1;

    if (fstatat(nfd, "", &obp->stat, AT_EMPTY_PATH) < 0) {
	close(nfd);
	return -1;
    }

    if (dup2(nfd, obp->fd) < 0) {
	close(nfd);
	return -1;
    }

    obp->flags = flags;

    if (close(nfd) < 0)
	return -1;

    return 0;
}

int
fsobj_delete(FSOBJ *obp) {
    int rc = fsobj_isopen(obp);
    if (rc < 0)
	return -1;
    else if (rc == 0) {
	return 0;
    }

    if (funlinkat(obp->parent->fd, obp->name, obp->fd, AT_RESOLVE_BENEATH|(S_ISDIR(obp->stat.st_mode) ? AT_REMOVEDIR : 0)) < 0)
	return -1;

    fsobj_reset(obp);
    return 0;
}



int
fsobj_rewind(FSOBJ *obp) {
    int rc = fsobj_isopen(obp);

    if (rc <= 0)
	return rc;

    obp->fdpos = 0;
    return lseek(obp->fd, obp->fdpos, SEEK_SET);
}

#define DIRBUFSIZE ((sizeof(struct dirent)+MAXNAMLEN+1)*256)

int
fsobj_readdir(FSOBJ *dirp, FSOBJ *objp) {
    ssize_t rc;
    size_t bufsize = DIRBUFSIZE;
    struct dirent *dep;


    /* Probably won't happen, but you never know */
    if (dirp->stat.st_blksize > bufsize)
	bufsize = dirp->stat.st_blksize;

    if (!dirp->dbuf) {
	dirp->dbuf = malloc(bufsize);
	if (!dirp->dbuf) {
	    return -1;
	}
	dirp->dbufpos = 0;
	dirp->dbuflen = 0;
    }

 Again:
    if (dirp->dbufpos >= dirp->dbuflen) {
	dirp->dbufpos = 0;

	rc = getdirentries(dirp->fd, dirp->dbuf, bufsize, &dirp->fdpos);
	if (rc <= 0) {
	    dirp->dbuflen = 0;
	    return rc;
	}

	dirp->dbuflen = rc;
    }

    dep = (struct dirent *) (dirp->dbuf+dirp->dbufpos);
    dirp->dbufpos += dep->d_reclen;

    if (dep->d_name[0] == '.' && (dep->d_name[1] == '\0' || (dep->d_name[1] == '.' && dep->d_name[2] == '\0')))
	goto Again;

    fsobj_reset(objp);
    rc = fsobj_open(objp, dirp, dep->d_name, O_PATH);

    return rc >= 0 ? 1 : -1;
}


int
fsobj_rename(FSOBJ *obp,
	     char *name) {
    int rc = 0;


    if (!obp || obp->magic != FSOBJ_MAGIC)
	abort();

    if (renameat(obp->parent ? obp->parent->fd : AT_FDCWD,
		 obp->name,
		 obp->parent ? obp->parent->fd : AT_FDCWD,
		 name) < 0) {
	return -1;
    }

    /* Update stat information for new object */
    if (fstatat(obp->fd, "", &obp->stat, AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW|AT_RESOLVE_BENEATH) < 0) {
	return -1;
    }

    free(obp->name);
    obp->name = strdup(name);
    if (!obp->name)
	abort();

    return rc;
}
