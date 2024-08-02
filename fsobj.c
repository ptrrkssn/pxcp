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

#include "config.h"
#include "fsobj.h"
#include "misc.h"

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
#include <sys/types.h>
#include <sys/time.h>

#ifndef AT_RESOLVE_BENEATH
#define AT_RESOLVE_BENEATH 0
#endif


static char *
_mode_type2str(mode_t m) {
    switch (m & S_IFMT) {
    case S_IFDIR:
        return "Directory";
    case S_IFREG:
        return "File";
    case S_IFLNK:
        return "Symlink";
    case S_IFSOCK:
        return "Socket(Unix)";
    case S_IFIFO:
        return "FIFO";
    case S_IFBLK:
        return "Device(Block)";
    case S_IFCHR:
        return "Device(Char)";
    default:
        return "?";
    }
}



/*
 * Prepare an object for usage
 */
void
fsobj_init(FSOBJ *op) {
    if (!op)
	abort();

    if (f_debug > 1)
        fprintf(stderr, "** fsobj_init(%p)\n", op);

    memset(op, 0, sizeof(*op));
    op->magic = FSOBJ_MAGIC;
    op->fd = -1;
}



/*
 * Reset an object for new usage
 *
 * Closes any open objects, releases allocated memory
 */
void
fsobj_reset(FSOBJ *op) {
    /* Sanity check */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
	abort();

    if (f_debug > 1)
        fprintf(stderr, "** fsobj_reset(%s): fd=%d refcnt=%lu parent=%s\n",
                fsobj_path(op), op->fd, op->refcnt, fsobj_path(op->parent));;

    /* Make sure no references to this object are active */
    if (op->refcnt > 0)
	abort();

#if defined(HAVE_GETDIRENTRIES)
    if (op->dbuf) {
	free(op->dbuf);
	op->dbuf = NULL;
	op->dbufpos = 0;
	op->dbuflen = 0;
        op->dbufsize = 0;
    }
#else
    if (op->dirp) {
        closedir(op->dirp);
        op->dirp = NULL;
        op->fd = -1;
    }
#endif
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

    
    op->flags = 0;
    memset(&op->stat, 0, sizeof(op->stat));
}



/* 
 * Reset and an object for no further use 
 */
void
fsobj_fini(FSOBJ *op) {
    /* Sanity check */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
	abort();

    if (f_debug > 1)
        fprintf(stderr, "** fsobj_fini(%p)\n", op);

    fsobj_reset(op);
    memset(op, 0, sizeof(*op));
}


/*
 * Return true if object points to a real object 
 */
int
fsobj_isreal(const FSOBJ *op) {
    /* Sanity check */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
	abort();

    return (op->fd >= 0);
}



int
fsobj_newref(FSOBJ *op,
	     FSOBJ *parent,
	     char *name) {
    int rc;

    
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
        abort();
    if (op->fd >= 0)
        abort();
    if (op->name)
        abort();


    if (f_debug > 1)
        fprintf(stderr, "** fsobj_newref(%p, \"%s\", \"%s\")\n",
                op, fsobj_path(parent), name);
    op->name = strdup(name);
    op->flags = O_PATH;

    if (op->parent)
        op->parent->refcnt++;

    rc = fsobj_stat(op, NULL, NULL);
    if (rc < 0 && errno == ENOENT)
        return 0;

    return 1;
}



/*
 * Open a new filesystem object.
 */
int
fsobj_open(FSOBJ *op,
           FSOBJ *dirp,
           const char *np,
	   int flags,
	   mode_t mode) {
    int fd, rc;
    char *path = NULL;
    const char *name;
    

    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
        abort();

    /* Already open? */
    if (op->fd >= 0) 
        abort();

    if (np) {
      fsobj_reset(op);
      name = np;
    } else {
      dirp = op->parent;
      name = op->name;
    }
    
    if (dirp && !S_ISDIR(dirp->stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    if (dirp && dirp->fd < 0) {
        /* Fake a new object */
        fd = -1;
        op->flags = flags;
        memset(&op->stat, 0, sizeof(op->stat));
        op->stat.st_mode = mode;
        goto End;
    }

    if ((flags & O_CREAT) != 0 && S_ISDIR(mode)) {
      fsobj_mkdir(dirp, name, mode);
      flags &= ~O_CREAT;
    }

#ifdef HAVE_OPENAT
    fd = openat(FSOBJ_FD(dirp), name, flags|O_NOFOLLOW, mode);
    if (f_debug)
        fprintf(stderr, "** fsobj_open(%s, %s, 0x%x, %04o): openat(%d, %s, 0x%x, %04o) -> %d (%s)\n",
                fsobj_path(dirp), np ? np : "NULL", flags, mode,
                FSOBJ_FD(dirp), name, flags|O_NOFOLLOW, mode,
                fd, fd < 0 ? strerror(errno) : "");
#else
    path = strdupcat(fsobj_path(dirp), "/", name, NULL);
    if (!path)
        abort();

    fd = open(path, flags|O_NOFOLLOW, mode);
    if (f_debug)
        fprintf(stderr, "** fsobj_open(%s, %s, 0x%x, %04o): open(%s, 0x%x, %04o) -> %d (%s)\n",
                fsobj_path(dirp), np ? np : "NULL", flags, mode,
                path,, flags|O_NOFOLLOW, mode,
                fd, fd < 0 ? strerror(errno) : "");
#endif
 End:
    if (path)
      free(path);
    
    if (fd < 0 && (flags & O_PATH) == 0)
        return -1;
    
    op->flags = flags;
    op->fd = fd;
    op->parent = dirp;
    if (op->parent)
      op->parent->refcnt++;
    op->name = strdup(name);
    
    rc = fsobj_stat(op, NULL, NULL);
    if (rc < 0) {
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_open(%s): fsobj_stat -> %d (%s)\n",
                    fsobj_path(op), rc, strerror(errno));
        return -1;
    }
    
    return (op->stat.st_mode&S_IFMT);
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
    if (!fsobj_isreal(a) || !fsobj_isreal(b))
	return -1;

    return ((a->stat.st_dev == b->stat.st_dev) &&
	    (a->stat.st_ino == b->stat.st_ino));
}




/*
 * Generate (and cache) a full path to the object
 */
char *
fsobj_path(FSOBJ *op) {
    size_t blen = 0;
    FSOBJ *tp;


    if (!op)
        return ".";
    
    /* Sanity checks */
    if (op->magic != FSOBJ_MAGIC)
        abort();


    if (op->path)
	return op->path;

    if (!op->name)
      return NULL;
      
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
    int rc;

    /* Sanity checks */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
        abort();

    memset(&sb, 0, sizeof(sb));
    rc = fsobj_stat(op, NULL, &sb);
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

    if (f_debug > 1)
        fprintf(stderr, "** fsobj_reopen(\"%s\", 0x%x)\n",
                fsobj_path(op), flags);
    
#if 0
    /* Already open with the right flags? */
    if (op->flags == flags && op->fd >= 0)
        return 0;
#endif
    if (op->parent && op->parent->fd != -1) {
        nfd = openat(op->parent->fd, op->name, (flags&~O_CREAT));
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_reopen(%s): openat(%d, \"%s\", 0x%x) -> %d (%s)\n",
                    fsobj_path(op),
                    op->parent->fd, op->name, (flags&~O_CREAT),
                    nfd, nfd < 0 ? strerror(errno) : "");
    } else {
        nfd = -1;
	errno = EBADF;
    }
              
    if (nfd < 0)
	return -1;

    if (nfd != op->fd) {
        if (dup2(nfd, op->fd) < 0) {
            close(nfd);
            return -1;
        }
        if (close(nfd) < 0)
            return -1;
    }
    
    op->flags = flags;
    if (fsobj_stat(op, NULL, NULL) < 0)
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

    if (f_debug > 1)
        fprintf(stderr, "** fsobj_close(%s) @ %d\n", fsobj_path(op), op->fd);

    if (op->fd >= 0) {
	close(op->fd);
	op->fd = -1;
    }

    op->fdpos = 0;
    op->flags = O_PATH;
}


char *
fsobj_typestr(FSOBJ *op) {
    if (!op)
        return "Null";
    if (op->magic != FSOBJ_MAGIC)
        return "Invalid";
    if (op->name == NULL)
        return "Init";

    return _mode_type2str(op->stat.st_mode);
}


int
fsobj_rewind(FSOBJ *op) {
    int rc = 0;
  
    /* Sanity checks */
    if (!op)
        abort();
    if (op->magic != FSOBJ_MAGIC)
        abort();

    if (op->fd >= 0)
        rc = lseek(op->fd, op->fdpos, SEEK_SET);

    op->fdpos = 0;
    return rc;
}



#define DIRBUFSIZE ((sizeof(struct dirent)+MAXNAMLEN+1)*256)


int
fsobj_readdir(FSOBJ *dp,
              FSOBJ *op) {
    ssize_t rc;
#ifdef HAVE_GETDIRENTRIES
    size_t bufsize = DIRBUFSIZE;
#endif
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

#ifndef HAVE_GETDIRENTRIES
    if (!dp->dirp) {
        if (dp->fd >= 0) {
            dp->dirp = fdopendir(dp->fd);
            if (f_debug > 1)
                fprintf(stderr, "** fsobj_readdir(\"%s\"): fdopendir(%d) -> %p (%s)\n",
                        fsobj_path(dp), dp->fd, dp->dirp, !dp->dirp ? strerror(errno) : "");
        } else {
            dp->dirp = opendir(fsobj_path(dp));
            if (f_debug > 1)
                fprintf(stderr, "** fsobj_readdir(\"%s\"): opendir(\"%s\") -> %p (%s)\n",
                        fsobj_path(dp), fsobj_path(dp), dp->dirp, !dp->dirp ? strerror(errno) : "");
        }

        if (!dp->dirp)
            return -1;
    }
    
 Again:
    dep = readdir(dp->dirp);
    if (!dep) {
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_readdir(\"%s\") -> NULL (%s)\n",
                    fsobj_path(dp), strerror(errno));
        return 0;
    }
#else
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
#endif

    if (dep->d_name[0] == '.' && (dep->d_name[1] == '\0' ||
				  (dep->d_name[1] == '.' && dep->d_name[2] == '\0')))
        goto Again;

    fsobj_reset(op);
    rc = fsobj_open(op, dp, dep->d_name, O_PATH, 0);

    return rc > 0 ? 1 : -1;
}



/*
 * Delete an object
 *
 * Arguments:
 *  op       Object (name == NULL) or Directory
 *  np       Name or NULL
 *
 * Returns:
 *   1       Object deleted
 *   0       Nothing to delete
 *  -1       Something went wrong
 */
int
fsobj_delete(FSOBJ *op,
             const char *np) {
    int rc;
    const char *name;
    char *path = NULL;
    FSOBJ *dirp = NULL;
    

    if (!op && !np)
        abort();

    if (op && op->magic != FSOBJ_MAGIC)
        abort();
    
    if (np) {
        dirp = op;
        name = np;
    } else {
        dirp = op->parent;
        name = op->name;
    }
    
    if (dirp && dirp->fd < 0) {
        /* Object & Parent not opened, fake a delete */
        rc = 0;
        if (f_debug > 1)
          fprintf(stderr, "** fsobj_delete(%s, %s): Virtual -> %d (%s)\n",
                  fsobj_path(op), np ? np : "NULL",
                  rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }
    
    if (dirp && !S_ISDIR(dirp->stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    
#ifdef HAVE_FUNLINKAT
    rc = funlinkat(FSOBJ_FD(dirp), name, op->fd, AT_RESOLVE_BENEATH|(S_ISDIR(op->stat.st_mode) ? AT_REMOVEDIR : 0));
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_delete(%s, %s): funlinkat(%d, %s, %d, AT_RESOLVE_BENEATH%s) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL",
                FSOBJ_FD(dirp), name, op->fd, (S_ISDIR(op->stat.st_mode) ? "|AT_REMOVEDIR" : ""),
                rc, rc < 0 ? strerror(errno) : "");
#else
    path = strdupcat(fsobj_path(dirp), "/", name, NULL);
    if (!path)
        abort();

    if (op == dirp || !S_ISDIR(op->stat.st_mode)) {
        rc = unlink(path);
        fprintf(stderr, "** fsobj_delete(%s, %s): unlink(%s) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL",
                path,
                rc, rc < 0 ? strerror(errno) : "");
        if (!rc || (rc < 0 && errno != EISDIR))
          goto End;
    }
        
    rc = rmdir(path);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_delete(%s, %s): rmdir(%s) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL",
                path, 
                rc, rc < 0 ? strerror(errno) : "");

#endif

 End:
    if (path)
        free(path);
    
    if (rc < 0)
        return rc;

    if (op != dirp) {
        close(op->fd);
        op->fd = -1;
        memset(&op->stat, 0, sizeof(op->stat));
        op->flags = O_PATH;
    }

    return (rc < 0 && errno == ENOENT) ? 0 : 1;
}




/*
 * Update owner & group of an object
 *
 * Arguments:
 *  op       Object (name == NULL) or Directory
 *  np       Name or NULL
 *  uid      Uid or -1
 *  gid      Gid or -1
 *
 * Returns:
 *   1       Updated
 *   0       No update done
 *  -1       Something went wrong
 */
int
fsobj_chown(FSOBJ *op,
            const char *np,
	    uid_t uid,
	    gid_t gid) {
    int rc;
    const char *name;
    char *path = NULL;
    FSOBJ *dirp = NULL;
  

    if (!op && !np)
        abort();

    if (op && op->magic != FSOBJ_MAGIC)
        abort();
    
    if (np) {
        dirp = op;
        name = np;
    } else {
        dirp = op->parent;
        name = op->name;
    }
    
    if (op != dirp && op->fd >= 0) {
        rc = fchown(op->fd, uid, gid);
        
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_chown(%s, %s, %d, %d): fchown(%d, %d, %d) -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL", uid, gid,
                    op->fd, uid, gid,
                    rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }

    if (dirp && !S_ISDIR(dirp->stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    
    if (dirp && dirp->fd < 0) {
      /* Object & Parent not opened, fake an update */
        rc = 0;
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_chown(%s, %s, %d, %d): Virtual -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL", uid, gid,
                    rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }
    
#ifdef HAVE_FCHOWNAT
    rc = fchownat(FSOBJ_FD(dirp), name, uid, gid, AT_SYMLINK_NOFOLLOW);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_chown(%s, %s, %d, %d): fchownat(%d, %s, %d, %d) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL", uid, gid,
                FSOBJ_FD(dirp), name, uid, gid,
                rc, rc < 0 ? strerror(errno) : "");
#else
    path = strdupcat(fsobj_path(dirp), "/", name, NULL);
    if (!path)
        abort();
        
    rc = lchown(path, uid, gid);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_chown(%s, %s, %d, %d): lchown(%s, %d, %d) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL", uid, gid,
                path, uid, gid,
                rc, rc < 0 ? strerror(errno) : "");
#endif
    
 End:
    if (path)
        free(path);

    if (rc < 0)
        return rc;

    if (op != dirp) {
        if (uid != -1 && op->stat.st_uid != uid) {
            op->stat.st_uid = uid;
            rc = 1;
        }
        if (gid != -1 && op->stat.st_gid != gid) {
            op->stat.st_uid = gid;
            rc = 1;
        }
        return rc;
    }

    return 1;
}


/*
 * Update atime, mtime & btime on an object
 *
 * Arguments:
 *  op       Object (name == NULL) or Directory
 *  np       Name or NULL
 *  tsv      Time vector
 *
 * Returns:
 *   1       Updated
 *   0       ?
 *  -1       Something went wrong
 */
int
fsobj_utimens(FSOBJ *op,
              const char *np,
	      struct timespec *tsv) {
    int rc;
    const char *name;
    char *path = NULL;
    FSOBJ *dirp = NULL;
  

    if (!op && !np)
        abort();

    if (op && op->magic != FSOBJ_MAGIC)
        abort();
    
    if (np) {
        dirp = op;
        name = np;
    } else {
        dirp = op->parent;
        name = op->name;
    }
    
    if (dirp && !S_ISDIR(dirp->stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    

#ifdef HAVE_FUTIMENS
    if (op != dirp && op->fd >= 0) {
        rc = futimens(op->fd, tsv);
        
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_utimens(%s, %s, %p): futimens(%d, %p) -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL", tsv,
                    op->fd, tsv,
                    rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }
#endif

    if (dirp && dirp->fd < 0) {
        /* Object & Parent not opened, fake an update */
        rc = 0;
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_utimens(%s, %s, %p): Virtual -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL", tsv,
                    rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }

#ifdef HAVE_UTIMENSAT
    rc = utimensat(FSOBJ_FD(dirp), name, tsv, AT_SYMLINK_NOFOLLOW);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_utimens(%s, %s, %p): utimensat(%d, %s, %p) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL", tsv,
                FSOBJ_FD(dirp), name, tsv,
                rc, rc < 0 ? strerror(errno) : "");
    goto End;
#else
    path = strdupcat(fsobj_path(dirp), "/", name, NULL);
    if (!path)
        abort();
    else {
        struct timeval tvb[2];
          
        tvb[0].tv_sec  = tsv[0].tv_sec;
        tvb[0].tv_usec = tsv[0].tv_nsec/1000;
        tvb[1].tv_sec  = tsv[1].tv_sec;
        tvb[1].tv_usec = tsv[1].tv_nsec/1000;
        
        rc = lutimes(path, &tvb[0]);
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_utimens(%s, %s, %p): lutimes(%s, %p) -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL", tsv,
                    path, &tvb[0],
                    rc, rc < 0 ? strerror(errno) : "");
    }
#endif

 End:
    if (path)
        free(path);

    if (rc < 0)
        return rc;

    if (op != dirp) {
        /* XXX: refresh via fsobj_stat? */
        switch (tsv[0].tv_nsec) {
        case UTIME_OMIT:
            break;
        case UTIME_NOW:
            break;
        default:
            op->stat.st_atim = tsv[0];
        }
        
        switch (tsv[1].tv_nsec) {
        case UTIME_OMIT:
            break;
        case UTIME_NOW:
            break;
        default:
            op->stat.st_mtim = tsv[1];
        }
    }
    
    return 1;
}


/*
 * Update mode bits of an object
 *
 * Arguments:
 *  op       Object (name == NULL) or Directory
 *  np       Name or NULL
 *  mode     New mode bits
 *
 * Returns:
 *   1       Updated
 *   0       No update neede
 *  -1       Something went wrong
 */
int
fsobj_chmod(FSOBJ *op,
            const char *np,
            mode_t mode) {
    int rc;
    const char *name;
    char *path = NULL;
    FSOBJ *dirp = NULL;

    
    if (!op && !np)
        abort();

    if (op && op->magic != FSOBJ_MAGIC)
        abort();
    
    if (np) {
        dirp = op;
        name = np;
    } else {
        dirp = op->parent;
        name = op->name;
    }
    
    if (dirp && !S_ISDIR(dirp->stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    

    if (op != dirp && op->fd >= 0) {
        rc = fchmod(op->fd, mode);
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_chmod(%s, %s, %04o): fchmod(%d, 0%o) -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL", mode,
                    op->fd, mode,
                    rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }

    if (dirp && dirp->fd < 0) {
        /* Object & Parent not opened, fake an update */
        rc = 0;
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_chmod(%s, %s, %04o): Virtual -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL", mode,
                    rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }
    
#ifdef HAVE_FCHMODAT
    rc = fchmodat(FSOBJ_FD(dirp), name, mode, AT_SYMLINK_NOFOLLOW);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_chmod(%s, %s, %04o): fchmodat(%d, %s, 0%o) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL", mode,
                FSOBJ_FD(dirp), name, mode,
                rc, rc < 0 ? strerror(errno) : "");
#else
    path = strdupcat(fsobj_path(dirp), "/", name, NULL);
    if (!path)
        abort();
        
    rc = lchmod(path, mode);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_chmod(%s, %s, %04o): lchmod(%s, 0%o) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL", mode,
                path, mode,
                rc, rc < 0 ? strerror(errno) : "");
#endif
    
 End:
    if (path)
        free(path);

    if (rc < 0)
        return rc;

    if (op != dirp) {
        if ((op->stat.st_mode&ALLPERMS) == mode)
            return 0;
    
        /* XXX: Full refresh with fsobj_stat() ? */
        op->stat.st_mode &= ~ALLPERMS;
        op->stat.st_mode |= (mode&ALLPERMS);
    }
    
    return 1;
}



/*
 * Rename an object inside a directory
 *
 * Arguments:
 *  op       Object (oldname == NULL) or Directory
 *  np       Old name or NULL
 *  newname  New name
 *
 * Returns:
 *   1       Renamed
 *   0       Same name
 *  -1       Something went wrong
 */
int
fsobj_rename(FSOBJ *op,
             const char *np,
             char *newname) {
    int rc;
    const char *oldname;
    char *oldpath = NULL;
    char *newpath = NULL;
    FSOBJ *dirp = NULL; 
    

    if (!op && !np)
        abort();

    if (op && op->magic != FSOBJ_MAGIC)
        abort();
    
    if (np) {
        dirp = op;
        oldname = np;
    } else {
        dirp = op->parent;
        oldname = op->name;
    }

    if (strcmp(oldname, newname) == 0)
        return 0;
    
    if (dirp && !S_ISDIR(dirp->stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    

    if (dirp && dirp->fd < 0) {
        /* Object & Parent not opened, fake a rename (below) */
        rc = 0;
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_rename(%s, %s, %s): Virtual -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL", newname,
                    rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }
    
#ifdef HAVE_RENAMEAT
    rc = renameat(FSOBJ_FD(dirp), oldname, FSOBJ_FD(dirp), newname);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_rename(%s, %s, %s): renameat(%d, %s, %d, %s) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL", newname, 
                FSOBJ_FD(dirp), oldname, FSOBJ_FD(dirp), newname,
                rc, rc < 0 ? strerror(errno) : "");
#else
    oldpath = strdupcat(fsobj_path(dirp), "/", oldname, NULL);
    if (!oldpath)
        abort();
    newpath = strdupcat(fsobj_path(dirp), "/", newname, NULL);
    if (!newpath)
        abort();
        
    rc = rename(oldpath, newpath);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_rename(%s, %s, %s): rename(%s, %s) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL", newname, 
                oldpath, newpath,
                rc, rc < 0 ? strerror(errno) : "");
#endif
        
 End:
    if (oldpath)
        free(oldpath);
    if (newpath)
        free(newpath);

    if (rc < 0)
        return rc;
    
    if (op != dirp) {
        free(op->name);
        op->name = strdup(newname);
        if (!op->name)
            abort();
        
        if (op->path) {
            free(op->path);
            op->path = NULL;
        }
    }
    
    return 1;
}



/*
 * Read stat information
 *
 * Arguments:
 *  op     Object (name == NULL) or Directory
 *  np     Name of Symlink or NULL
 *  sp     Pointer to stat struct
 *
 * Returns:
 *   1     Data returned
 *   0     Object not found
 *  -1     Something went wrong
 */
int
fsobj_stat(FSOBJ *op,
           const char *np,
	   struct stat *sp) {
    int rc;
    const char *name;
    char *path = NULL;
    FSOBJ *dirp = NULL;
    struct stat sb;
    
    
    if (!op && !np)
        abort();

    if (op && op->magic != FSOBJ_MAGIC)
        abort();

    if (!sp)
        sp = &sb;
    
    if (np) {
        dirp = op;
        name = np;
    } else {
        dirp = op->parent;
        name = op->name;
    }
    
    if (dirp && !S_ISDIR(dirp->stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    

    if (op->fd >= 0) {
        rc = fstat(op->fd, sp);
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_stat(%s, %s): fstat(%d) -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL",
                    op->fd,
                    rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }

    if (dirp && dirp->fd < 0) {
        /* Object & Parent not opened */
        errno = ENOENT;
        rc = -1;
        if (f_debug > 1)
          fprintf(stderr, "** fsobj_stat(%s, %s): Virtual -> %d (%s)\n",
                  fsobj_path(op), np ? np : "NULL",
                  rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }
    
#ifdef HAVE_FSTATAT
    rc = fstatat(FSOBJ_FD(dirp), name, sp, AT_SYMLINK_NOFOLLOW);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_stat(%s, %s): fstatat(%d, %s) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL",
                FSOBJ_FD(dirp), name, 
                rc, rc < 0 ? strerror(errno) : "");
#else
    path = strdupcat(fsobj_path(dirp), "/", name, NULL);
    if (!path)
        abort();

    rc = lstat(path, sp);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_stat(%s, %s): lstat(\"%s\") -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL",
                path, 
                rc, rc < 0 ? strerror(errno) : "");
#endif
    
 End:
    if (path)
        free(path);

    if (rc < 0) {
        if (errno == ENOENT)
            return 0;
        else
            return rc;
    }

    if (op != dirp) {
        op->stat = *sp;
    }
    
    return 1; /* XXX or return sp->st_mode? */
}


/*
 * Read a symlink
 *
 * Arguments:
 *  op       Object (name == NULL) or Directory
 *  np       Name of Symlink or NULL
 *  bufp     Output buffer
 *  bufsize  Size of output buffer
 *
 * Returns:
 *  >0     Length of symlink target
 *  -1     Something went wrong
 */
ssize_t
fsobj_readlink(FSOBJ *op,
               const char *np,
	       char *bufp,
	       size_t bufsize) {
    ssize_t rc;
    const char *name;
    char *path = NULL;
    FSOBJ *dirp = NULL;
    

    if (!op && !np)
        abort();

    if (op && op->magic != FSOBJ_MAGIC)
        abort();
    

    if (np) {
        dirp = op;
        name = np;
    } else {
        dirp = op->parent;
        name = op->name;
    }
    
#ifdef HAVE_FREADLINK
    if (op->fd >= 0) {
        rc = freadlink(op->fd, bufp, bufsize);
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_readlink(%s, %s): freadlink -> %lld (%s)\n",
                    fsobj_path(op), np ? np : "NULL",
                    (long long int) rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }
#endif
    
    if (dirp && !S_ISDIR(dirp->stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    
    if (dirp && dirp->fd < 0) {
        /* Object & Parent not opened */
      errno = ENOENT;
      rc = -1;
      if (f_debug > 1)
          fprintf(stderr, "** fsobj_readlink(%s, %s): Virtual -> %lld (%s)\n",
                  fsobj_path(op), np ? np : "NULL",
                  (long long int) rc, rc < 0 ? strerror(errno) : "");
      goto End;
    }
    
#ifdef HAVE_READLINKAT
    rc = readlinkat(FSOBJ_FD(dirp), name, bufp, bufsize);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_readlink(%s, %s): readlinkat(%d, %s) -> %lld (%s)\n",
                fsobj_path(op), np ? np : "NULL",
                FSOBJ_FD(dirp), name,
                (long long int) rc, rc < 0 ? strerror(errno) : "");
#else
    path = strdupcat(fsobj_path(dirp), "/", name, NULL);
    if (!path)
        abort();
    
    rc = readlink(fsobj_path(op), bufp, bufsize);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_readlink(%s, %s): readlink -> %lld (%s)\n",
                fsobj_path(op), np ? np : "NULL",
                (long long int) rc, rc < 0 ? strerror(errno) : "");
#endif
    
 End:
    if (path)
        free(path);
    
    return rc;
}


/*
 * Create a symlink
 *
 * Arguments:
 *  op       Object (name == NULL) or Directory
 *  np       Name of Symlink or NULL
 *  target   Symlink content
 *
 * Returns:
 *   1       Created
 *   0       A symlink already exists
 *  -1       Something went wrong
 */
int
fsobj_symlink(FSOBJ *op,
              const char *np,
              const char *target) {
    int rc;
    const char *name;
    char *path = NULL;
    FSOBJ *dirp = NULL;
    

    if (!op && !np)
        abort();

    if (op && op->magic != FSOBJ_MAGIC)
        abort();
    
    if (!target)
        abort();


    if (np) {
        dirp = op;
        name = np;
    } else {
        dirp = op->parent;
        name = op->name;
    }
    
    if (dirp && !S_ISDIR(dirp->stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    if (dirp && dirp->fd < 0) {
        /* Object & Parent not opened */
        rc = 0;
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_symlink(%s, %s, %s): Virtual -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL", target,
                    rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }
    
#ifdef HAVE_SYMLINKAT
    rc = symlinkat(target, FSOBJ_FD(dirp), name);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_symlink(%s, %s, %s): symlinkat(\"%s\", %d, \"%s\") -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL", target,
                target, FSOBJ_FD(dirp), name,
                rc, rc < 0 ? strerror(errno) : "");
#else
    path = strdupcat(fsobj_path(dirp), "/", name, NULL);
    if (!path)
        abort();
    
    rc = symlink(target, path);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_symlink(%s, %s, %s): symlink(\"%s\", \"%s\") -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL", target,
                target, path,
                rc, rc < 0 ? strerror(errno) : "");
#endif
    
 End:
    if (path)
        free(path);

    if (rc < 0 && errno != EEXIST)
        return -1;

    if (op != dirp) {
        /* XXX: Update st_mode via fsobj_stat? */
        op->stat.st_mode |= S_IFLNK;
    }
    
    return (rc < 0 ? 0 : 1);
}


/*
 * Create a directory
 *
 * Arguments:
 *  op     Object (name == NULL) or Directory
 *  np     Name of Subdirectory or NULL
 *  mode   Permissions
 *
 * Returns:
 *   1     Created
 *   0     Already exists
 *  -1     Something went wrong
 */
int
fsobj_mkdir(FSOBJ *op,
            const char *np,
            mode_t mode) {
    int rc;
    const char *name;
    char *path = NULL;
    FSOBJ *dirp = NULL;
    
    
    if (!op && !np)
        abort();

    if (op && op->magic != FSOBJ_MAGIC)
        abort();
   
 
    if (np) {
        dirp = op;
        name = np;
    } else {
        dirp = op->parent;
        name = op->name;
    }
    
    if (dirp && !S_ISDIR(dirp->stat.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    if (dirp && dirp->fd < 0) {
        /* Object & Parent not opened */
        rc = 0;
        if (f_debug > 1)
            fprintf(stderr, "** fsobj_mkdir(%s, %s, %04o): Virtual -> %d (%s)\n",
                    fsobj_path(op), np ? np : "NULL", mode,
                    rc, rc < 0 ? strerror(errno) : "");
        goto End;
    }
    
#ifdef HAVE_MKDIRAT
    rc = mkdirat(FSOBJ_FD(dirp), name, mode);
    if (f_debug > 1)
        fprintf(stderr, "** fsobj_mkdir(%s, %s, %04o): mkdirat(%d, %s, %04o) -> %d (%s)\n",
                fsobj_path(op), np ? np : "NULL", mode,
                FSOBJ_FD(dirp), name, mode,
                rc, rc < 0 ? strerror(errno) : "");
#else
    path = strdupcat(fsobj_path(dirp), "/", name, NULL);    
    if (!path)
        abort();

    rc = mkdir(path, mode);
    if (f_debug > 1)
      fprintf(stderr, "** fsobj_mkdir(%s, %s, %04o): mkdir(%s, %04o) -> %d (%s)\n",
              fsobj_path(op), np ? np : "NULL", mode,
              path, mode,
              rc, rc < 0 ? strerror(errno) : "");
#endif
    
 End:
    if (path)
        free(path);
    
    if (rc < 0 && errno != EEXIST)
        return -1;

    if (op != dirp) {
        /* XXX: Update st_mode via fsobj_stat? */
        op->stat.st_mode |= S_IFDIR|mode;
    }
    
    return (rc < 0 ? 0 : 1);
}
