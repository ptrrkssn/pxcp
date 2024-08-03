/*
 * fsobj.h
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

#ifndef PXCP_FSOBJ_H
#define PXCP_FSOBJ_H 1

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#ifndef AT_RESOLVE_BENEATH
#define AT_RESOLVE_BENEATH 0
#endif

#ifndef O_DIRECT
#define O_DIRECT 0
#endif

#ifndef O_PATH
#ifdef O_SYMLINK
/* MacOS doesn't have O_PATH, but we emulate it with O_SYMLINK */
#define O_PATH O_SYMLINK
#endif
#endif

#define FSOBJ_FD(op) (op ? op->fd : AT_FDCWD)


#define FSOBJ_MAGIC 0x5729043
#define FSOBJ_MAXREFS 256

typedef struct fsobj {
    int magic;
    struct fsobj *parent;
    char *name;
    char *path;
    int fd;
    off_t fdpos;
    int flags;
    struct stat stat;
    size_t refcnt;
    struct fsobj *refs[FSOBJ_MAXREFS];
#ifdef HAVE_GETDIRENTRIES
    char *dbuf;
    off_t dbufpos;
    size_t dbuflen;
    size_t dbufsize;
#else
    DIR *dirp;
#endif
} FSOBJ;


extern void
fsobj_init(FSOBJ *obp);

extern void
fsobj_reset(FSOBJ *obp);

extern void
fsobj_fini(FSOBJ *obp);

extern int
fsobj_isreal(const FSOBJ *obp);

extern int
fsobj_typeof(const FSOBJ *objp);



extern int
fsobj_open(FSOBJ *op,
           FSOBJ *dp,
           const char *name,
	   int flags,
	   mode_t mode);

extern int
fsobj_refresh(FSOBJ *op);

extern void
fsobj_close(FSOBJ *obp);


extern int
fsobj_equal(const FSOBJ *a,
	    const FSOBJ *b);

extern char *
fsobj_path(FSOBJ *obp);

extern int
fsobj_reopen(FSOBJ *obp,
	     int flags);

extern int
fsobj_rewind(FSOBJ *obp);

extern char *
fsobj_typestr(FSOBJ *op);


extern int
fsobj_readdir(FSOBJ *dirp,
	      FSOBJ *objp);


extern int
fsobj_delete(FSOBJ *op,
             const char *np);



extern int
fsobj_chown(FSOBJ *op,
            const char *np,
	    uid_t uid,
	    gid_t gid);

extern int
fsobj_utimens(FSOBJ *op,
              const char *np,
	      struct timespec *tv);

extern int
fsobj_chmod(FSOBJ *op,
            const char *np,
            mode_t mode);

extern int
fsobj_rename(FSOBJ *op,
             const char *np,
	     char *newname);

extern int
fsobj_stat(FSOBJ *op,
           const char *name,
	   struct stat *sp);

extern ssize_t
fsobj_readlink(FSOBJ *op,
               const char *name,
	       char *bufp,
	       size_t bufsize);

extern int
fsobj_symlink(FSOBJ *op,
              const char *name,
              const char *target);

extern int
fsobj_mkdir(FSOBJ *op,
            const char *name,
            mode_t mode);

extern int
fsobj_chflags(FSOBJ *op,
              const char *np,
              unsigned long flags);

#endif
