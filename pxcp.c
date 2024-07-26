/*
 * pxcp.c
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

#ifdef __linux__
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <time.h>
#include <sys/mman.h>

#if HAVE_SYS_EXTATTR_H
#include <sys/extattr.h>
#endif

#include "acls.h"
#include "fsobj.h"

#ifndef AT_RESOLVE_BENEATH
#define AT_RESOLVE_BENEATH 0
#endif

#ifndef MAP_NOCORE
#define MAP_NOCORE 0
#endif

int f_debug = 0;
int f_verbose = 0;
int f_noxdev = 0;
int f_dryrun = 0;
int f_recurse = 0;
int f_create = 0;
int f_metaonly = 0;
int f_warnings = 0;
int f_silent = 0;
int f_ignore = 0;
int f_times = 0;
int f_owner = 0;
int f_group = 0;
int f_acl = 0;
int f_xattr = 0;
int f_prune = 0;
int f_force = 0;
int f_mmap = 0;
int f_all = 0;

int my_groups = 0;
gid_t *my_groupv = NULL;

struct options {
    char c;
    int *vp;
    char *help;
} options[] = {
    { 'a', &f_all,       "Archive mode (enables c,g,o,r,t,A,X options)" },
    { 'c', &f_create,    "Create new objects" },
    { 'd', &f_debug,     "Set debugging outputlevel" },
    { 'f', &f_force,     "Force updates" },
    { 'g', &f_group,     "Copy object group" },
    { 'h', NULL,         "Display usage information" },
    { 'm', &f_metaonly,  "Only copy metadata" },
    { 'n', &f_dryrun,    "Enable dry-run mode" },
    { 'o', &f_owner,     "Copy object owner" },
    { 'p', &f_prune,     "Prune removed objects" },
    { 'r', &f_recurse,   "Enable recursion" },
#if 0
    { 's', &f_silent,    "Be silent" },
#endif
    { 't', &f_times,     "Copy modfication times" },
    { 'v', &f_verbose,   "Set verbosity level" },
    { 'w', &f_warnings,  "Display warnings/notices" },
    { 'x', &f_noxdev,    "Do not cross filesystems" },
    { 'A', &f_acl,       "Copy ACLs" },
    { 'M', &f_mmap,      "Use mmap(2)" },
    { 'X', &f_xattr,     "Copy Extended Attributes" },
    { -1,  NULL,         NULL }
};

char *argv0 = "pxcp";

FSOBJ root_srcdir, root_dstdir;

unsigned long n_scanned = 0;
unsigned long n_added = 0;
unsigned long n_updated = 0;
unsigned long n_deleted = 0;

#define MD_NEW    0x0001
#define MD_DATA   0x0010
#define MD_BTIME  0x0020
#define MD_MTIME  0x0040
#define MD_MODE   0x0100
#define MD_UID    0x0200
#define MD_GID    0x0400
#define MD_ACL    0x1000
#define MD_ATTR   0x2000

#define MD_CONTENT  0x00F0
#define MD_METADATA 0xFF00


#if !HAVE_FUNLINKAT
#define funlinkat(dirfd,name,fd,flags) unlinkat(dirfd,name,flags)
#endif


int
in_grouplist(gid_t g) {
    int i;

    for (i = 0; i < my_groups; i++)
	if (g == my_groupv[i])
	    return 1;
    return 0;
}


int
ts_isless(struct timespec *a,
	  struct timespec *b) {
    if (a->tv_sec == b->tv_sec)
        return a->tv_nsec < b->tv_nsec;
    return a->tv_sec < b->tv_sec;
}



int
file_copyto(FSOBJ *src,
	     FSOBJ *dst,
	     FSOBJ *dstdir) {
    char *tmpname = ".pxcp_tmpfile"; /* XXX: Make dynamic */
    ssize_t got, rc = 0;
    FSOBJ t_obj;
    char *d_name, *bufp = NULL, buf[65536];


    if (f_debug) {
	fprintf(stderr, "*** file_copyto(%s -> %s%s%s)\n",
		fsobj_path(src),
		dstdir ? fsobj_path(dstdir) : fsobj_path(dst),
		dstdir ? "/" : "",
		dstdir ? src->name : "");
    }

    if (dstdir) {
	d_name = src->name;
    } else {
	dstdir = dst->parent;
	d_name = dst->name;
    }

    if (src->flags & O_PATH)
	fsobj_reopen(src, O_RDONLY|O_NOFOLLOW|O_DIRECT);

    if (!f_dryrun) {
	fsobj_init(&t_obj);

	if (fsobj_open(&t_obj, dstdir, tmpname, (f_create ? O_CREAT : 0)|O_WRONLY|O_DIRECT, 0600) <= 0) {
	    fprintf(stderr, "%s: Error: %s/%s: create: %s\n",
		    argv0, fsobj_path(dstdir), tmpname, strerror(errno));
	    rc = -1;
	    goto End;
	}
    }

    if (src->stat.st_size > 0) {
	if (f_mmap) {
	    bufp = mmap(NULL, src->stat.st_size, PROT_READ, MAP_NOCORE|MAP_PRIVATE, src->fd, 0);
	    if (bufp == MAP_FAILED) {
		fprintf(stderr, "%s: Error: %s: mmap(fd=%d, size=%ld): %s\n",
			argv0, fsobj_path(src),
			src->fd,
			src->stat.st_size,
			strerror(errno));
		return -1;
	    }

	    madvise(bufp, src->stat.st_size, MADV_SEQUENTIAL|MADV_WILLNEED);

	    if (!f_dryrun) {
		rc = write(t_obj.fd, bufp, src->stat.st_size);
		if (rc < 0) {
		    fprintf(stderr, "%s: Error: %s: write: %s\n",
			    argv0, fsobj_path(&t_obj), strerror(errno));
		    rc = -1;
		    goto End;
		}
		if (rc != src->stat.st_size) {
		    fprintf(stderr, "%s: Error: %s/%s: Short write (%ld of %ld bytes)\n",
			    argv0, fsobj_path(&t_obj), tmpname,
			    rc, src->stat.st_size);
		    rc = -1;
		    goto End;
		}
	    }
	} else {
	    while ((got = read(src->fd, buf, sizeof(buf))) > 0) {
		if (!f_dryrun) {
		    rc = write(t_obj.fd, buf, got);
		    if (rc < 0) {
			fprintf(stderr, "%s: Error: %s: write: %s\n",
				argv0, fsobj_path(&t_obj),
				strerror(errno));
			rc = -1;
			goto End;
		    }
		    if (rc != got) {
			fprintf(stderr, "%s: Error: %s: Short write (%ld of %ld bytes)\n",
				argv0, fsobj_path(&t_obj), rc, got);
			rc = -1;
			goto End;
		    }
		}
	    }
	    if (got < 0) {
		fprintf(stderr, "%s: Error: %s: read: %s\n",
			argv0, fsobj_path(src), strerror(errno));
		rc = -1;
		goto End;
	    }
	}
    }

    if (!f_dryrun) {
	if (fsobj_rename(&t_obj, d_name) < 0) {
	    fprintf(stderr, "%s: Error: %s -> %s: Rename: %s\n",
		    argv0, fsobj_path(src), d_name, strerror(errno));
	    rc = -1;
	    goto End;
	}
    }

    fsobj_reset(dst);
    if (!f_dryrun)
	*dst = t_obj;
    else
	dst->stat.st_size = src->stat.st_size;

    fsobj_reopen(dst, O_PATH);

 End:
    if (rc)
	(void) unlinkat(t_obj.parent->fd, tmpname, AT_RESOLVE_BENEATH);

    if (bufp) {
	if (f_mmap)
	    munmap(bufp, src->stat.st_size);
	else
	    free(bufp);
    }

    return rc;
}




int
dir_prune(FSOBJ *srcdir,
	  FSOBJ *dstdir) {
    int rc = 0, s_type = -1;
    FSOBJ s_obj, d_obj;


    if (f_debug) {
	fprintf(stderr, "*** dir_prune(%s%s%s)\n",
		srcdir ? fsobj_path(srcdir) : "-",
		srcdir ? " <- " : "",
		dstdir ? fsobj_path(dstdir) : "-");
    }

    /* Reopen destination directory for reading */
    if ((dstdir->flags & O_PATH) != 0) {
	if (fsobj_reopen(dstdir, O_RDONLY|O_DIRECTORY|O_NOFOLLOW) < 0)
	    return -1;
    }


    /* Reopen for reading if source object is directory */
    if (srcdir && S_ISDIR(srcdir->stat.st_mode) && (srcdir->flags & O_PATH) != 0) {
	if (fsobj_reopen(srcdir, O_RDONLY|O_DIRECTORY|O_NOFOLLOW) < 0)
	    return -1;
    }


    fsobj_init(&s_obj);
    fsobj_init(&d_obj);

    while ((rc = fsobj_readdir(dstdir, &d_obj)) > 0) {
	int d_type = fsobj_typeof(&d_obj);

	if (d_type <= 0) {
	    if (f_ignore)
		goto Next;
	    else {
		rc = -1;
		goto End;
	    }
	}

	/* Skip if noxdev enabled and object is on another filesystem */
	if (f_noxdev && dstdir->stat.st_dev != d_obj.stat.st_dev)
	    goto Next;

	if (srcdir) {
	    s_type = fsobj_open(&s_obj, srcdir, d_obj.name, O_PATH);

	    /* Skip if -xx enabled and source object is on a another filesystem */
	    if (f_noxdev > 1 && srcdir->stat.st_dev != s_obj.stat.st_dev) {
		fsobj_reset(&s_obj);
		s_type = 0;
	    }
	} else
	    s_type = 0;

	if (f_recurse && S_ISDIR(d_obj.stat.st_mode)) {
	    /* Recurse down */

	    if (fsobj_equal(&d_obj, &root_srcdir)) {
		fprintf(stderr, "%s: Error: %s: Infinite recursion\n",
			argv0, fsobj_path(&d_obj));
		rc = -1;
		goto End;
	    }

	    if (dir_prune(s_type > 0 ? &s_obj : NULL, &d_obj) < 0) {
		if (f_ignore)
		    goto Next;
		else {
		    rc = -1;
		    goto End;
		}
	    }
	}

	/* Delete destination object if source does not exist */
	if (s_type == 0) {
	    if (!f_dryrun) {
		if (fsobj_delete(&d_obj) < 0) {
		    fprintf(stderr, "%s: Error: %s: Delete: %s\n",
			    argv0, fsobj_path(&d_obj), strerror(errno));

		    if (f_ignore)
			goto Next;
		    else {
			rc = -1;
			goto End;
		    }
		}
		++n_deleted;

		if (f_verbose)
		    printf("- %s\n", fsobj_path(&d_obj));
	    }
	}

    Next:
	fsobj_reset(&d_obj);
	fsobj_reset(&s_obj);
    }

 End:
    fsobj_fini(&d_obj);
    fsobj_fini(&s_obj);

    return rc;
}


/* Clone extended attributes */
int
attrs_clone(FSOBJ *src,
	    FSOBJ *dst) {
#if HAVE_EXTATTR_GET_FD
    ssize_t s_alen, d_alen = 0;
    char *s_alist = NULL, *d_alist = NULL;
    int rc = 0, s_i, d_i = 0;
    int d_open = fsobj_isopen(dst);


    /* Get list of extended attribute from source */
    s_alen = extattr_list_fd(src->fd, EXTATTR_NAMESPACE_USER, NULL, 0);
    if (s_alen < 0) {
	fprintf(stderr, "%s: Error: %s: extattr_list_fd: %s\n",
		argv0, fsobj_path(src), strerror(errno));
	return -1;
    }

    s_alist = (char *) malloc(s_alen);
    if (!s_alist) {
	fprintf(stderr, "%s: Error: %s: malloc(%ld): %s\n",
		argv0, fsobj_path(src), s_alen, strerror(errno));
	rc = -1;
	goto End;
    }

    s_alen = extattr_list_fd(src->fd, EXTATTR_NAMESPACE_USER, s_alist, s_alen);
    if (s_alen < 0) {
	fprintf(stderr, "%s: Error: %s: extattr_list_fd: %s\n",
		argv0, fsobj_path(src), strerror(errno));
	return -1;
    }


    if (d_open) {
	/* Get list of extended attribute from destination */
	d_alen = extattr_list_fd(dst->fd, EXTATTR_NAMESPACE_USER, NULL, 0);
	if (d_alen < 0) {
	    fprintf(stderr, "%s: Error: %s: extattr_list_fd: %s\n",
		    argv0, fsobj_path(dst), strerror(errno));
	    return -1;
	}

	d_alist = (char *) malloc(d_alen);
	if (!d_alist) {
	    fprintf(stderr, "%s: Error: %s: malloc(%ld): %s\n",
		    argv0, fsobj_path(dst), d_alen, strerror(errno));
	    rc = -1;
	    goto End;
	}

	d_alen = extattr_list_fd(dst->fd, EXTATTR_NAMESPACE_USER, d_alist, d_alen);
	if (d_alen < 0) {
	    fprintf(stderr, "%s: Error: %s: extattr_list_fd: %s\n",
		    argv0, fsobj_path(dst), strerror(errno));
	    return -1;
	}

	/* Scan for attributes to remove */
	d_i = 0;
	while (d_i < d_alen) {
	    unsigned char d_anlen = d_alist[d_i++];

	    if (d_anlen == 0)
		continue;

	    /* Scan for matching attribute */
	    s_i = 0;
	    while (s_i < s_alen) {
		unsigned char s_anlen = s_alist[s_i++];
		if (s_anlen == 0)
		    continue;

		if (d_anlen == s_anlen && memcmp(s_alist+s_i, d_alist+d_i, s_anlen) == 0)
		    break;
	    }
	    if (s_i >= s_alen) {
		if (!f_dryrun) {
		    char *aname = strndup((char *) d_alist+d_i, d_anlen);

		    if (!aname) {
			fprintf(stderr, "%s: Error: %s: %.*s: strndup: %s\n",
				argv0, fsobj_path(dst), d_anlen, d_alist+d_i, strerror(errno));
			rc = -1;
			goto End;
		    }

		    if (extattr_delete_fd(dst->fd, EXTATTR_NAMESPACE_USER, aname) < 0) {
			fprintf(stderr, "%s: Error: %s: %s: extattr_delete: %s\n",
				argv0, fsobj_path(dst), aname, strerror(errno));
			free(aname);
			aname = NULL;
			rc = -1;
			goto End;
		    }
		    free(aname);
		    aname = NULL;
		}
		if (f_verbose > 1)
		    printf("  - %s : %.*s\n", fsobj_path(dst), d_anlen, d_alist+d_i);
		rc = 1;
	    }

	    d_i += d_anlen;
	}
    }


    s_i = 0;
    while (s_i < s_alen) {
	char *aname;
	ssize_t s_adlen, d_adlen;
	void *s_adata = NULL, *d_adata = NULL;
	unsigned char s_anlen = s_alist[s_i++];

	if (s_anlen == 0)
	    continue;

	if (d_open) {
	    /* Scan for matching attribute */
	    d_i = 0;
	    while (d_i < d_alen) {
		unsigned char d_anlen = d_alist[d_i++];

		if (d_anlen == 0)
		    continue;

		if (s_anlen == d_anlen && memcmp(s_alist+s_i, d_alist+d_i, s_anlen) == 0)
		    break;
	    }
	}

	aname = strndup(s_alist+s_i, s_anlen);
	if (!aname) {
	    fprintf(stderr, "%s: Error: %s: %.*s: strndup: %s\n",
		    argv0, fsobj_path(src), s_anlen, s_alist+s_i, strerror(errno));
	    rc = -1;
	    goto End;
	}

	s_adlen = extattr_get_fd(src->fd, EXTATTR_NAMESPACE_USER, aname, NULL, 0);
	if (s_adlen < 0) {
	    fprintf(stderr, "%s: Error: %s: %s: extattr_get_fd: %s\n",
		    argv0, fsobj_path(src), aname, strerror(errno));
	    free(aname);
	    aname = NULL;
	    rc = -1;
	    goto End;
	}

	s_adata = malloc(s_adlen);
	if (!s_adata) {
	    fprintf(stderr, "%s: Error: %s: %.*s: malloc(%ld): %s\n",
		    argv0, fsobj_path(src), s_anlen, s_alist+s_i, s_adlen, strerror(errno));
	    rc = -1;
	    goto End;
	}

	s_adlen = extattr_get_fd(src->fd, EXTATTR_NAMESPACE_USER, aname, s_adata, s_adlen);
	if (s_adlen < 0) {
	    fprintf(stderr, "%s: Error: %s: %s: extattr_get_fd: %s\n",
		    argv0, fsobj_path(src), aname, strerror(errno));
	    free(aname);
	    aname = NULL;
	    rc = -1;
	    goto End;
	}

	if (d_open) {
	    if (d_i < d_alen) {
		d_adlen = extattr_get_fd(dst->fd, EXTATTR_NAMESPACE_USER, aname, NULL, 0);
		if (d_adlen < 0) {
		    fprintf(stderr, "%s: Error: %s: %s: extattr_get_fd: %s\n",
			    argv0, fsobj_path(dst), aname, strerror(errno));
		    free(aname);
		    aname = NULL;
		    rc = -1;
		    goto End;
		}

		/* Only bother to read and check content if same lenght */
		if (s_adlen == d_adlen) {
		    d_adata = malloc(d_adlen);
		    if (!d_adata) {
			fprintf(stderr, "%s: Error: %s: %s: malloc(%ld): %s\n",
				argv0, fsobj_path(dst), aname, d_adlen, strerror(errno));
			free(aname);
			aname = NULL;
			rc = -1;
			goto End;
		    }

		    d_adlen = extattr_get_fd(dst->fd, EXTATTR_NAMESPACE_USER, aname, d_adata, d_adlen);
		    if (d_adlen < 0) {
			fprintf(stderr, "%s: Error: %s: %s: extattr_get_fd: %s\n",
				argv0, fsobj_path(dst), aname, strerror(errno));
			free(aname);
			aname = NULL;
			rc = -1;
			goto End;
		    }

		    if (memcmp(d_adata, s_adata, s_adlen) != 0) {
			if (!f_dryrun) {
			    if (extattr_set_fd(dst->fd, EXTATTR_NAMESPACE_USER, aname, s_adata, s_adlen) < 0) {
				fprintf(stderr, "%s: Error: %s: %s: extattr_set_fd: %s\n",
					argv0, fsobj_path(dst), aname, strerror(errno));
				free(aname);
				aname = NULL;
				rc = -1;
				goto End;
			    }
			}

			if (f_verbose > 1)
			    printf("  ! %s : %s\n", fsobj_path(dst), aname);
			rc = 1;
		    }
		    free(d_adata);
		    d_adata = NULL;
		} else {
		    if (!f_dryrun) {
			if (extattr_set_fd(dst->fd, EXTATTR_NAMESPACE_USER, aname, s_adata, s_adlen) < 0) {
			    fprintf(stderr, "%s: Error: %s: %s: extattr_set_fd: %s\n",
				    argv0, fsobj_path(dst), aname, strerror(errno));
			    free(aname);
			    aname = NULL;
			    rc = -1;
			    goto End;
			}
		    }
		    if (f_verbose > 1)
			printf("  ! %s : %s\n", fsobj_path(dst), aname);
		    rc = 1;
		}
	    } else {
		if (!f_dryrun) {
		    if (extattr_set_fd(dst->fd, EXTATTR_NAMESPACE_USER, aname, s_adata, s_adlen) < 0) {
			fprintf(stderr, "%s: Error: %s: %s: extattr_set_fd: %s\n",
				argv0, fsobj_path(dst), aname, strerror(errno));
			free(aname);
			aname = NULL;
			rc = -1;
			goto End;
		    }
		}
		if (f_verbose > 1)
		    printf("  + %s : %s\n", fsobj_path(dst), aname);
		rc = 1;
	    }
	}

	if (s_adata) {
	    free(s_adata);
	    s_adata = NULL;
	}
	if (aname) {
	    free(aname);
	    aname = NULL;
	}

	s_i += s_anlen;
    }

 End:
    if (s_alist)
	free(s_alist);
    if (d_alist)
	free(d_alist);

    return rc;
#else
    errno = ENOSYS;
    return -1;
#endif
}


char *
ts_text(struct timespec *ts,
	char *buf,
	size_t bufsize) {
    char *cp;
    struct tm *tp;

    tp = localtime(&ts->tv_sec);
    strftime(buf, bufsize, "%F %T", tp);

    cp = buf;
    while (*cp) {
	++cp;
	--bufsize;
    }
    snprintf(cp, bufsize, ".%09lu", ts->tv_nsec);
    return buf;
}

void
dir_list(FSOBJ *dirp) {
    FSOBJ obj;
    int rc, n;

    n = 0;
    fsobj_init(&obj);
    while ((rc = fsobj_readdir(dirp, &obj)) > 0)
	fprintf(stderr, "%d. %s\n", ++n, fsobj_path(&obj));
    fsobj_fini(&obj);
}


int
copy_times(FSOBJ *src,
	   FSOBJ *dst) {
    int rc = 0;

#if HAVE_UTIMENSAT
#if HAVE_STRUCT_STAT_ST_BIRTHTIM_TV_SEC
    if (f_force || ts_isless(&dst->stat.st_birthtim, &src->stat.st_birthtim)) {
	if (!f_dryrun) {
	    struct timespec times[2];

	    times[0].tv_nsec = UTIME_OMIT;
	    times[1] = src->stat.st_birthtim;
	    if (utimensat(dst->fd, "", times, AT_EMPTY_PATH) < 0) {
		fprintf(stderr, "%s: Error: %s: utimes(btime): %s\n",
			argv0, fsobj_path(dst), strerror(errno));
		rc = -1;
		goto End;
	    }
	}
	rc |= MD_BTIME;
    }
#endif
    if (f_force || ts_isless(&dst->stat.st_mtim, &src->stat.st_mtim)) {
	if (!f_dryrun) {
	    struct timespec times[2];
	    
	    times[0].tv_nsec = UTIME_OMIT;
	    times[1] = src->stat.st_mtim;

	    if (utimensat(dst->fd, "", times, AT_EMPTY_PATH) < 0) {
		fprintf(stderr, "%s: Error: %s: utimes(mtime): %s\n",
			argv0, fsobj_path(dst), strerror(errno));
		rc = -1;
		goto End;
	    }
	}
	rc |= MD_MTIME;
    }
#else
    errno = ENOSYS;
    rc = -1;
#endif

 End:
    return rc;
}


int
dir_clone(FSOBJ *srcdir,
	  FSOBJ *dstdir) {
    int rc = 0;
    acl_t s_acl = NULL, d_acl = NULL;
    FSOBJ s_obj, d_obj;


    if (f_debug) {
	fprintf(stderr, "*** dir_clone: %s -> %s\n",
		fsobj_path(srcdir),
		fsobj_path(dstdir));
    }

    if (!fsobj_isopen(srcdir)) {
	fprintf(stderr, "%s: Error: %s: Not open & readable\n",
		argv0, fsobj_path(srcdir));
	return -1;
    }

    if (!S_ISDIR(srcdir->stat.st_mode)) {
	fprintf(stderr, "%s: Error: %s: Not a Directory\n",
		argv0, fsobj_path(srcdir));
	return -1;
    }

    /* Reopen destination directory for reading (if needed) */
    if (fsobj_isopen(dstdir) && (dstdir->flags & O_PATH) != 0) {
	if (f_debug)
	    fprintf(stderr, "%s: Reopening Destination Directory\n",
		    fsobj_path(dstdir));
	if (fsobj_reopen(dstdir, O_RDONLY|O_DIRECTORY|O_NOFOLLOW) < 0) {
	    fprintf(stderr, "%s: Error: %s: Reopen Directory: %s\n",
		    argv0, fsobj_path(dstdir), strerror(errno));
	    return -1;
	}
    }

    /* Reopen for reading if source object is directory (if needed) */
    if ((srcdir->flags & O_PATH) != 0) {
	if (f_debug)
	    fprintf(stderr, "%s: Reopening Source Directory\n",
		    fsobj_path(srcdir));
	if (fsobj_reopen(srcdir, O_RDONLY|O_DIRECTORY|O_NOFOLLOW) < 0) {
	    fprintf(stderr, "%s: Error: %s: Reopen Directory: %s\n",
		    argv0, fsobj_path(srcdir), strerror(errno));
	    return -1;
	}
    }

    fsobj_init(&s_obj);
    fsobj_init(&d_obj);

    if (f_prune) {
#if 0
	dir_prune(srcdir, dstdir);
#else
	if (f_debug)
	    fprintf(stderr, "%s: Pruning Destination\n",
		    fsobj_path(dstdir));
	
	while ((rc = fsobj_readdir(dstdir, &d_obj)) > 0) {
	    int s_type = fsobj_open(&s_obj, srcdir, d_obj.name, O_PATH);
	    if (s_type == 0) {
		if (!f_dryrun) {
		    if (fsobj_typeof(&d_obj) == S_IFDIR) {
			if (dir_prune(NULL, &d_obj) < 0) {
			    rc = -1;
			    goto End;
			}
		    }
		    if (fsobj_delete(&d_obj) < 0) {
			fprintf(stderr, "%s: Error: %s: Delete: %s\n",
				argv0, fsobj_path(&d_obj), strerror(errno));
			rc = -1;
			goto End;
		    }
		}
		++n_deleted;
		if (f_verbose)
		    printf("- %s\n", fsobj_path(&d_obj));
	    }
	}
	fsobj_reset(&s_obj);
	fsobj_reset(&d_obj);
    }
#endif

    fsobj_rewind(dstdir);
    fsobj_rewind(srcdir);

    if (f_debug)
	fprintf(stderr, "%s: Scanning Source\n",
		fsobj_path(srcdir));

    while ((rc = fsobj_readdir(srcdir, &s_obj)) > 0) {
	unsigned int mdiff = 0;
	int s_type = -1, d_type = -1;

	s_type = fsobj_typeof(&s_obj);
	if (s_type <= 0) {
	    fprintf(stderr, "%s: Error: %s: Unknown object type\n",
		    argv0, fsobj_path(&s_obj));
	    rc = -1;
	    goto End;
	}

	if (f_debug)
	    fprintf(stderr, " -- got source object: %s (type %d)\n",
		    s_obj.name, s_type);

	if (fsobj_isopen(dstdir))
	    d_type = fsobj_open(&d_obj, dstdir, s_obj.name, O_PATH);
	else
	    d_type = 0;

	if (f_debug)
	    fprintf(stderr, " -- found destination object: %s (type %d, fd %d, errno %d)\n",
		    d_obj.name, d_type, d_obj.fd, errno);

	if (d_type > 0 && s_type != d_type) {
	    /* Different type objects, delete target and recreate */

	    if (f_debug)
		fprintf(stderr, "** %s -> %s: Object types differs (%o vs %o) - removing destination\n",
			fsobj_path(&s_obj),
			fsobj_path(&d_obj),
			s_obj.stat.st_mode&S_IFMT,
			d_obj.stat.st_mode&S_IFMT);

	    if (!f_metaonly) {
		if (d_type == S_IFDIR) {
		    /* If directory, make sure it's empty */
		    if (dir_prune(NULL, &d_obj) < 0) {
			rc = -1;
			goto End;
		    }
		}

		if (!f_dryrun) {
		    /* Remove the destination object */
		    if (fsobj_delete(&d_obj) < 0) {
			fprintf(stderr, "%s: Error: %s: Delete: %s\n",
				argv0, fsobj_path(&d_obj), strerror(errno));
			rc = -1;
			goto End;
		    }
		}
		
		if (f_verbose)
		    printf("- %s\n", fsobj_path(&d_obj));
	    }
		
	    fsobj_reset(&d_obj);
	    d_type = 0;
	}

	if (d_type == 0) {
	    /* Destination object does not exist */
	    if (!f_create)
		goto Next;
	    
	    if (!f_metaonly) {
		if (!f_dryrun) {
		    switch (s_type) {
		    case S_IFDIR:
			if (f_debug)
			    fprintf(stderr, "** %s/%s: Destination is new directory\n",
				    fsobj_path(dstdir),
				    s_obj.name);
			
			if (fsobj_open(&d_obj, dstdir, s_obj.name, (f_create ? O_CREAT : 0)|O_DIRECTORY,
				       (s_obj.stat.st_mode&ALLPERMS)) < 0) {
			    fprintf(stderr, "%s: Error: %s/%s: mkdirat: %s\n",
				    argv0, fsobj_path(dstdir), s_obj.name, strerror(errno));
			    rc = -1;
			    goto End;
			}
			break;
			
		    case S_IFREG:
			if (f_debug)
			    fprintf(stderr, "** %s/%s: Destination is new file\n",
				    fsobj_path(dstdir),
				    s_obj.name);
			
			if (!f_metaonly) {
			    if (file_copyto(&s_obj, &d_obj, dstdir) < 0) {
				rc = -1;
				goto End;
			    }
			}
			break;
			
		    case S_IFLNK:
			char pbuf[PATH_MAX+1];
			ssize_t plen;
			
			if (f_debug)
			    fprintf(stderr, "** %s/%s: Destination is new symbolic link\n",
				    fsobj_path(dstdir),
				    s_obj.name);
			
			plen = readlinkat(srcdir->fd, s_obj.name, pbuf, PATH_MAX);
			if (plen < 0) {
			    fprintf(stderr, "%s: Error: %s: readlinkat: %s\n",
				    argv0, fsobj_path(&s_obj), strerror(errno));
			    rc = -1;
			    goto End;
			}
			pbuf[plen] = '\0';
			
			if (symlinkat(pbuf, dstdir->fd, s_obj.name) < 0) {
			    fprintf(stderr, "%s: Error: %s/%s -> %s: Create(symlink): %s\n",
				    argv0, fsobj_path(dstdir), s_obj.name,
				    pbuf,
				    strerror(errno));
			    rc = -1;
			    goto End;
			}
			if (fsobj_open(&d_obj, dstdir, s_obj.name, O_PATH) < 0) {
			    fprintf(stderr, "%s: Error: %s/%s: Open(symlink): %s\n",
				    argv0, fsobj_path(dstdir), s_obj.name,
				    strerror(errno));
			    rc = -1;
			    goto End;
			}
			break;
			
		    default:
			fprintf(stderr, "%s: Error: %s: Unhandled file type: %o\n",
				argv0, fsobj_path(&s_obj), s_type);
			rc = -1;
			goto End;
		    }
		} else {
		    /* Dry-run, simulate that we created the same object */
		    d_type = fsobj_fake(&d_obj, dstdir, &s_obj);
		    
		    if (f_debug)
			fprintf(stderr, "%s <- %s: Faked object: type=%d\n",
				fsobj_path(&d_obj), fsobj_path(&s_obj), d_type);
		}
		mdiff |= MD_NEW;
	    }
	} else {
	    char s_tbuf[256], d_tbuf[256];

	    if (f_debug)
		fprintf(stderr, "%s / %s: size %ld vs %ld, time %s vs %s\n",
			fsobj_path(&s_obj),
			fsobj_path(&d_obj),
			s_obj.stat.st_size,
			d_obj.stat.st_size,
			ts_text(&s_obj.stat.st_mtim, s_tbuf, sizeof(s_tbuf)),
			ts_text(&d_obj.stat.st_mtim, d_tbuf, sizeof(d_tbuf)));


	    switch (s_type) {
	    case S_IFREG:
		if (f_force || s_obj.stat.st_size != d_obj.stat.st_size ||
		    ts_isless(&d_obj.stat.st_mtim, &s_obj.stat.st_mtim)) {

		    if (!f_dryrun && !f_metaonly) {
			if (file_copyto(&s_obj, &d_obj, NULL) < 0) {
			    rc = -1;
			    goto End;
			}
		    }
		    mdiff |= MD_DATA;
		}
		break;

	    case S_IFLNK:
		char s_buf[PATH_MAX+1];
		char d_buf[PATH_MAX+1];
		ssize_t s_len, d_len;

		/* XXX: f_metaonly - how to handle symlinks? */
		
		s_len = readlinkat(s_obj.parent->fd, s_obj.name, s_buf, PATH_MAX);
		if (s_len < 0) {
		    fprintf(stderr, "%s: Error: %s: readlinkat: %s\n",
			    argv0, fsobj_path(&s_obj), strerror(errno));
		    rc = -1;
		    goto End;
		}
		s_buf[s_len] = '\0';

		d_len = readlinkat(d_obj.parent->fd, s_obj.name, d_buf, PATH_MAX);
		if (d_len < 0) {
		    fprintf(stderr, "%s: Error: %s: readlinkat: %s\n",
			    argv0, fsobj_path(&d_obj), strerror(errno));
		    rc = -1;
		    goto End;
		}
		d_buf[d_len] = '\0';

		if (s_len != d_len || strcmp(s_buf, d_buf) != 0) {
		    if (!f_dryrun) {
			if (symlinkat(s_buf, d_obj.parent->fd, s_obj.name) < 0) {
			    fprintf(stderr, "%s: Error: %s -> %s: symlinkat: %s\n",
				    argv0,
				    fsobj_path(&d_obj),
				    fsobj_path(&s_obj),
				    strerror(errno));
			    rc = -1;
			    goto End;
			}
		    }
		    mdiff |= MD_DATA;
		}
		break;

	    case S_IFDIR:
		break;

	    default:
		fprintf(stderr, "%s: Error: %s/%s: Unhandled file type: %o\n",
			argv0, fsobj_path(srcdir), s_obj.name, s_type);
		rc = -1;
		goto End;
	    }
	}

	if (f_owner && (f_force || s_obj.stat.st_uid != d_obj.stat.st_uid)) {
	    if (geteuid() != 0) {
		if (f_warnings)
		    fprintf(stderr, "%s: Warning: %s: Owner Change not Permitted\n",
			    argv0, fsobj_path(&d_obj));;
	    } else {
		if (!f_dryrun) {
		    if (fchownat(d_obj.fd, "", s_obj.stat.st_uid, -1, AT_EMPTY_PATH) < 0) {
			fprintf(stderr, "%s: Error: %s: fchownat(uid=%d): %s\n",
				argv0,
				fsobj_path(&d_obj),
				s_obj.stat.st_uid,
				strerror(errno));
			rc = -1;
			goto End;
		    }
		    }
		mdiff |= MD_UID;
	    }
	}
	
	if (f_group && (f_force || s_obj.stat.st_gid != d_obj.stat.st_gid)) {
	    if (geteuid() != 0 && !in_grouplist(s_obj.stat.st_gid)) {
		if (f_warnings)
		    fprintf(stderr, "%s: Warning: %s: Group Change not Permitted\n",
			    argv0, fsobj_path(&d_obj));;
	    } else {
		if (!f_dryrun) {
		    if (fchownat(d_obj.fd, "", -1, s_obj.stat.st_gid, AT_EMPTY_PATH) < 0) {
			fprintf(stderr, "%s: Error: %s: fchownat(gid=%d): %s\n",
				argv0,
				fsobj_path(&d_obj),
				s_obj.stat.st_gid,
				strerror(errno));
			rc = -1;
			goto End;
		    }
		}
		mdiff |= MD_GID;
	    }
	}

	if (f_force || (s_obj.stat.st_mode&ALLPERMS) != (d_obj.stat.st_mode&ALLPERMS)) {
	    if (!f_dryrun) {
#if __linux__
                  if (fchmodat(d_obj.parent->fd, d_obj.name, (s_obj.stat.st_mode&ALLPERMS), AT_SYMLINK_NOFOLLOW) < 0) {
#else
                  if (fchmodat(d_obj.fd, "", (s_obj.stat.st_mode&ALLPERMS), AT_EMPTY_PATH) < 0) {
#endif
		    fprintf(stderr, "%s: Error: %s: fchmodat(0%o): %s\n",
			    argv0,
			    fsobj_path(&d_obj),
			    (s_obj.stat.st_mode&ALLPERMS),
			    strerror(errno));
		    rc = -1;
		    goto End;
		}
	    }
	    mdiff |= MD_MODE;
	}

	if (f_xattr) {
	    int arc = attrs_clone(&s_obj, &d_obj);

	    if (arc < 0) {
		rc = -1;
		goto End;
	    } else if (arc > 0)
		mdiff |= MD_ATTR;
	}

	if (f_acl && d_type >= 0) {
#if HAVE_ACL_GET_FD_NP
	    acl_type_t s_t, d_t;

	    s_acl = acl_get_fd_np(s_obj.fd, s_t = ACL_TYPE_NFS4);
	    if (!s_acl)
		s_acl = acl_get_fd_np(s_obj.fd, s_t = ACL_TYPE_ACCESS);

	    d_acl = acl_get_fd_np(d_obj.fd, d_t = ACL_TYPE_NFS4);
	    if (!d_acl)
		d_acl = acl_get_fd_np(d_obj.fd, d_t = ACL_TYPE_ACCESS);

	    if (s_acl && (f_force || !d_acl || (rc = acl_diff(s_acl, d_acl)))) {
		if (!f_dryrun) {
		    fsobj_reopen(&d_obj, O_RDONLY|O_NOFOLLOW);
		    if (acl_set_fd_np(d_obj.fd, s_acl, s_t) < 0) {
			fprintf(stderr, "%s: Error: %s: acl_set_fd_np(fd=%d): %s\n",
				argv0, fsobj_path(&d_obj),
				d_obj.fd, strerror(errno));
			rc = -1;
			goto End;
		    }
		}
		mdiff |= MD_ACL;
	    }
#endif
	}

	if (f_times) {
	    int trc = copy_times(&s_obj, &d_obj);
	    if (trc > 0)
		mdiff |= trc;
	}

	if (f_verbose > 2 || (f_verbose && mdiff)) {
	    if (mdiff & MD_NEW)
		putchar('+');
	    else if (mdiff & (MD_DATA|MD_MTIME|MD_BTIME|MD_ATTR|MD_ACL|MD_MODE|MD_UID|MD_GID))
		putchar('!');
	    else
		putchar(' ');
	    if (f_verbose > 1)
		printf(" %s ->", fsobj_path(&s_obj));
	    printf(" %s", fsobj_path(&d_obj));
	    if (f_verbose > 1)
		printf(" [%04x]", mdiff);
	    putchar('\n');
	}

	++n_scanned;
	if (mdiff & MD_NEW)
	    ++n_added;
	else if (mdiff)
	    ++n_updated;

	if (f_recurse && s_type == S_IFDIR) {
	    if (!f_noxdev || srcdir->stat.st_dev == s_obj.stat.st_dev) {
		int t_rc;

		if (fsobj_equal(&s_obj, &root_dstdir)) {
		    if (f_warnings)
			fprintf(stderr, "%s: Notice: %s: Infinite recursion - skipping\n",
				argv0, fsobj_path(&s_obj));
		    goto Next;
		}

		t_rc = dir_clone(&s_obj, &d_obj);
		if (t_rc < 0) {
		    rc = -1;
		    goto End;
		}
		if (f_times)
		    if (copy_times(&s_obj, &d_obj) < 0) {
			fprintf(stderr, "%s: Error: %s -> %s: Copying times: %s\n",
				argv0, fsobj_path(&s_obj), fsobj_path(&d_obj), strerror(errno));
			exit(1);
		    }
	    }
	    else {
		if (f_warnings)
		    fprintf(stderr, "%s: Notice: %s: Skipping recursing down due to noxdev (%ld vs %ld)\n",
			    argv0, fsobj_path(&s_obj), s_obj.stat.st_dev, srcdir->stat.st_dev);
		goto Next;
	    }
	}


    Next:
	if (s_acl) {
	    acl_free(s_acl);
	    s_acl = NULL;
	}

	if (d_acl) {
	    acl_free(d_acl);
	    d_acl = NULL;
	}

	fsobj_reset(&s_obj);
	fsobj_reset(&d_obj);
    }

 End:
    if (s_acl)
	acl_free(s_acl);

    if (d_acl)
	acl_free(d_acl);

    fsobj_fini(&s_obj);
    fsobj_fini(&d_obj);

    return rc;
}




int
main(int argc,
     char *argv[]) {
    int i, j, k, rc;
    time_t t0, t1;
    double dt;
    char *tu = "s";


    argv0 = argv[0];

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
	for (j = 1; argv[i][j]; j++) {
            int rv, *vp = NULL;
            char c;
	    
	    for (k = 0; options[k].c != -1 && options[k].c != argv[i][j]; k++)
		;
	    if (options[k].c == -1) {
		fprintf(stderr, "%s: Error: -%c: Invalid option\n",
			argv[0], argv[i][j]);
		exit(1);
	    }

	    vp = options[k].vp;
	    if (!vp) {
		printf("Usage:\n  %s [options] <src> <dst>\n\nOptions:\n", argv0);
		for (k = 0; options[k].c != -1; k++)
		    printf("  -%c    %s\n", options[k].c, options[k].help);
		exit(0);
	    }
	    
	    rv = sscanf(argv[i]+j+1, "%d%c", vp, &c);
	    if (rv < 0 && isdigit(argv[i][j+1])) {
		fprintf(stderr, "%s: Error: %s: Scanning number: %s\n",
			argv0, argv[i]+j+1, strerror(errno));
		exit(1);
	    }
	    
	    switch (rv) {
	    case -1:
	    case 0:
		 (*vp)++;
		 break;
	    case 1:
		break;
	    case 2:
		switch (tolower(c)) {
		case 'k':
		    *vp *= 1000;
		    break;
		case 'm':
		    *vp *= 1000000;
		    break;
		case 'g':
		    *vp *= 1000000000;
		    break;
		case 't':
		    *vp *= 1000000000000;
		    break;
		default:
		    fprintf(stderr, "%s: Error: -%s: Invalid prefix\n",
			    argv0, argv[i]+j);
		    exit(1);
		}
		break;
	    }
	}
    }

    if (f_all) {
	f_create  += f_all;
	f_group   += f_all;
	f_owner   += f_all;
	f_recurse += f_all;
	f_times   += f_all;
	f_acl     += f_all;
	f_xattr   += f_all;
    }

    if (f_verbose)
	printf("[%s - Copyright (c) Peter Eriksson <pen@lysator.liu.se>]\n",
	       PACKAGE_STRING);

    if (i >= argc) {
	fprintf(stderr, "%s: Error: Missing required <src> <dst> arguments\n",
		argv[0]);
	exit(1);
    }

    if (i+1 >= argc) {
	fprintf(stderr, "%s: Error: Missing required <dst> argument\n",
		argv[0]);
	exit(1);
    }

    if (f_group && geteuid() != 0) {
	my_groups = getgroups(0, NULL);

	my_groupv = calloc(my_groups, sizeof(gid_t));
	if (!my_groupv) {
	    fprintf(stderr, "%s: Error: %ld: Memory Allocation Failure: %s\n",
		    argv0, my_groups*sizeof(gid_t), strerror(errno));
	    exit(1);
	}
    }

    fsobj_init(&root_srcdir);
    fsobj_init(&root_dstdir);

    time(&t0);

    if (fsobj_open(&root_srcdir, NULL, argv[i], O_RDONLY|O_DIRECTORY) <= 0) {
	fprintf(stderr, "%s: Error: %s: open: %s\n",
		argv[0], argv[i], strerror(errno));
	exit(1);
    }

    if (fsobj_open(&root_dstdir, NULL, argv[i+1], (f_create ? O_CREAT : 0)|O_RDONLY|O_DIRECTORY,
		   root_srcdir.stat.st_mode&ALLPERMS) <= 0) {
	fprintf(stderr, "%s: Error: %s: open: %s\n",
		argv[0], argv[i+1], strerror(errno));
	exit(1);
    }
    /* XXX: Clone times/acl/extattrs for top-level */

    rc = dir_clone(&root_srcdir, &root_dstdir);
    if (rc)
	goto End;

 End:
    fsobj_fini(&root_srcdir);
    fsobj_fini(&root_dstdir);

    time(&t1);
    dt = difftime(t1,t0);
    if (dt > 90.0) {
	dt /= 60;
	tu = "m";
    }
    if (dt > 90.0) {
	dt /= 60;
	tu = "h";
    }
    if (f_verbose)
	printf("[%lu scanned in %.1f %s; %lu added, %lu updated, %lu deleted]\n",
	       n_scanned, dt, tu, n_added, n_updated, n_deleted);
    exit(rc ? 1 : 0);
}
