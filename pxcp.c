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

#ifdef HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_SEC
#define st_mtim st_mtimespec
#endif

#ifndef MAP_NOCORE
#define MAP_NOCORE 0
#endif

int f_debug = 0;
int f_verbose = 0;
int f_noxdev = 0;
int f_dryrun = 0;
int f_sync = 0;
int f_recurse = 0;
int f_exist = 0;
int f_metaonly = 0;
int f_warnings = 0;
int f_silent = 0;
int f_ignore = 0;
int f_times = 0;
int f_owners = 0;
int f_groups = 0;
int f_acls = 0;
int f_xattrs = 0;
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
    { 'd', &f_debug,     "Set debugging outputlevel" },
    { 'e', &f_exist,     "Only copy to existing targets" },
    { 'f', &f_force,     "Force updates" },
    { 'g', &f_groups,    "Copy object group" },
    { 'h', NULL,         "Display usage information" },
    { 'm', &f_metaonly,  "Only copy metadata" },
    { 'n', &f_dryrun,    "Enable dry-run mode" },
    { 'o', &f_owners,    "Copy object owner" },
    { 'p', &f_prune,     "Prune removed objects" },
    { 'r', &f_recurse,   "Enable recursion" },
    { 's', &f_sync,      "Set sync mode" },
    { 't', &f_times,     "Copy modfication times" },
    { 'v', &f_verbose,   "Set verbosity level" },
    { 'w', &f_warnings,  "Display warnings/notices" },
    { 'x', &f_noxdev,    "Do not cross filesystems" },
    { 'A', &f_acls,      "Copy ACLs" },
    { 'M', &f_mmap,      "Use mmap(2)" },
    { 'X', &f_xattrs,    "Copy Extended Attributes" },
    { -1,  NULL,         NULL }
};

char *argv0 = "pxcp";

FSOBJ root_srcdir, root_dstdir;

unsigned long n_scanned = 0;
unsigned long n_added = 0;
unsigned long n_updated = 0;
unsigned long n_deleted = 0;

#define MD_NEW    0x0001
#define MD_DEL    0x0002
#define MD_DATA   0x0010
#define MD_TIME   0x0020
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


ssize_t
symlink_clone(FSOBJ *src,
              FSOBJ *dst) {
    char s_pbuf[PATH_MAX+1], d_pbuf[PATH_MAX+1];
    ssize_t s_plen, d_plen;
    int rc = 0;

    s_plen = readlinkat(src->parent->fd, src->name, s_pbuf, PATH_MAX);
    if (s_plen < 0) {
        fprintf(stderr, "%s: Error: %s: Read(symlink): %s\n",
                argv0, fsobj_path(src), strerror(errno));
        return -1;
    }
    s_pbuf[s_plen] = '\0';
    
    d_plen = readlinkat(dst->parent->fd, dst->name, d_pbuf, PATH_MAX);
    if (d_plen < 0) {
	if (errno != ENOENT) {
	    fprintf(stderr, "%s: Error: %s: Read(symlink): %s\n",
		    argv0, fsobj_path(dst), strerror(errno));
	    return -1;
	}
    }
    if (d_plen >= 0)
	d_pbuf[d_plen] = '\0';

    if (f_force || d_plen < 0 || strcmp(s_pbuf, d_pbuf) != 0) {
	if (!f_dryrun) {
	    if (d_plen >= 0 && unlinkat(dst->parent->fd, dst->name, AT_RESOLVE_BENEATH) < 0) {
		fprintf(stderr, "%s: Error: %s: Unlink(symlink): %s\n",
			argv0, fsobj_path(dst), strerror(errno));
		return -1;
	    }
	    
	    if (symlinkat(s_pbuf, dst->parent->fd, dst->name) < 0) {
		fprintf(stderr, "%s: Error: %s -> %s: Create(symlink): %s\n",
			argv0, fsobj_path(dst), s_pbuf, strerror(errno));
		return -1;
	    }
	}
	rc = 1;
    }

    if (fsobj_refresh(dst) < 0) {
        fprintf(stderr, "%s: Error: %s: Refresh(symlink): %s\n",
                argv0, fsobj_path(dst), strerror(errno));
        return -1;
    }

    return rc;
}


ssize_t
file_clone(FSOBJ *src,
           FSOBJ *dst) {
    ssize_t wr, rc = 0;
    char *bufp = MAP_FAILED;
    char *tmpname = ".pxcp_tmpfile"; /* XXX: Make dynamic */
    int tfd = -1;


    if (f_force || src->stat.st_size != dst->stat.st_size || ts_isless(&dst->stat.st_mtim, &src->stat.st_mtim)) {
        if (src->flags & O_PATH)
            fsobj_reopen(src, O_RDONLY);

        if (!f_dryrun) {
            tfd = openat(dst->parent->fd, tmpname, O_CREAT|O_EXCL|O_WRONLY, 0400);
            if (tfd < 0) {
                fprintf(stderr, "%s: Error: %s/%s: Create(tmpfile): %s\n",
                        argv0, fsobj_path(dst->parent), tmpname, strerror(errno));
                return -1;
            }
        }

        if (src->stat.st_size > 0) {
            bufp = mmap(NULL, src->stat.st_size, PROT_READ, MAP_NOCORE|MAP_PRIVATE, src->fd, 0);
            if (bufp == MAP_FAILED) {
                fprintf(stderr, "%s: Error: %s: mmap: %s\n",
                        argv0, fsobj_path(src), strerror(errno));
                rc = -1;
                goto End;
            }

            /* Ignore errors */
            (void) madvise(bufp, src->stat.st_size, MADV_SEQUENTIAL|MADV_WILLNEED);

            if (!f_dryrun) {
                wr = write(tfd, bufp, src->stat.st_size);
                if (wr < 0) {
                    int t_errno = errno;

                    fprintf(stderr, "%s: Error: %s/%s: Write: %s\n",
                            argv0, fsobj_path(dst->parent), tmpname, strerror(errno));
                    munmap(bufp, src->stat.st_size);
                    errno = t_errno;
                    rc = -1;
                    goto End;
                }
                if (wr != src->stat.st_size) {
                    fprintf(stderr, "%s: Error: %s/%s: Short write\n",
                            argv0, fsobj_path(dst->parent), tmpname);
                    errno = EPIPE;
                    rc = -1;
                    goto End;
                }
                close(tfd);

                if (renameat(dst->parent->fd, tmpname, dst->parent->fd, dst->name) < 0) {
                    int t_errno = errno;

                    fprintf(stderr, "%s: Error: %s/%s -> %s/%s: Rename: %s\n",
                            argv0,
                            fsobj_path(dst->parent), tmpname,
                            fsobj_path(dst->parent), dst->name,
                            strerror(errno));
                    errno = t_errno;
                    rc = -1;
                    goto End;
                }
                tmpname = NULL;

                fsobj_reopen(dst, O_PATH);
            }
            rc = 1;
        }
    }

 End:

    if (bufp)
        munmap(bufp, src->stat.st_size);
    if (tfd >= 0)
        close(tfd);
    if (tmpname)
        (void) unlinkat(dst->parent->fd, tmpname, 0);

    return rc;
}



int
dir_prune2(FSOBJ *srcdir,
           FSOBJ *dstdir) {
    int rc = 0, s_type = -1;
    FSOBJ s_obj, d_obj;


    if (f_debug) {
	fprintf(stderr, "*** dir_prune2(%s%s%s)\n",
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
	    s_type = fsobj_open(&s_obj, srcdir, d_obj.name, O_PATH, 0);

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

	    if (dir_prune2(s_type > 0 ? &s_obj : NULL, &d_obj) < 0) {
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


int
dir_prune(FSOBJ *dp) {
    int rc = 0;
    FSOBJ d_obj;


    if (f_debug) {
	fprintf(stderr, "*** dir_prune(%s)\n",
		fsobj_path(dp));
    }

    /* Reopen destination directory for reading */
    if ((dp->flags & O_PATH) != 0) {
	if (fsobj_reopen(dp, O_RDONLY|O_DIRECTORY) < 0)
	    return -1;
    }


    fsobj_init(&d_obj);
    fsobj_rewind(dp);

    while ((rc = fsobj_readdir(dp, &d_obj)) > 0) {
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
	if (f_noxdev && dp->stat.st_dev != d_obj.stat.st_dev)
	    goto Next;

	if (f_recurse && S_ISDIR(d_obj.stat.st_mode)) {
	    /* Recurse down */

	    if (fsobj_equal(&d_obj, &root_srcdir)) {
		fprintf(stderr, "%s: Error: %s: Infinite recursion\n",
			argv0, fsobj_path(&d_obj));
		rc = -1;
		goto End;
	    }

	    if (dir_prune(&d_obj) < 0) {
		if (f_ignore)
		    goto Next;
		else {
		    rc = -1;
		    goto End;
		}
	    }
	}

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
            ++n_scanned;
            ++n_deleted;
            if (f_verbose)
                printf("- %s\n", fsobj_path(&d_obj));
        }

    Next:
        fsobj_reset(&d_obj);
    }

 End:
    fsobj_fini(&d_obj);
    return rc;
}


int
owner_clone(FSOBJ *src,
             FSOBJ *dst) {
    int rc = 0;


    if (f_force || src->stat.st_uid != dst->stat.st_uid) {
        if (geteuid() != 0) {
            if (f_warnings)
                fprintf(stderr, "%s: Warning: %s: Owner Change not Permitted\n",
                        argv0, fsobj_path(dst));
        } else {
            if (!f_dryrun) {
                if (fchownat(dst->fd, "", src->stat.st_uid, -1, AT_EMPTY_PATH) < 0) {
                    fprintf(stderr, "%s: Error: %s: fchownat(uid=%d): %s\n",
                            argv0,
                            fsobj_path(dst),
                            src->stat.st_uid,
                            strerror(errno));
                    rc = -1;
                    goto End;
                }
            }
            rc = 1;
	}
    }

 End:
    return rc;
}

int
group_clone(FSOBJ *src,
            FSOBJ *dst) {
    int rc = 0;


    if (f_force || src->stat.st_gid != dst->stat.st_gid) {
        if (geteuid() != 0 && !in_grouplist(src->stat.st_gid)) {
            if (f_warnings)
                fprintf(stderr, "%s: Warning: %s: Group Change not Permitted\n",
                        argv0, fsobj_path(dst));
        } else {
            if (!f_dryrun) {
                if (fchownat(dst->fd, "", -1, src->stat.st_gid, AT_EMPTY_PATH) < 0) {
                    fprintf(stderr, "%s: Error: %s: fchownat(gid=%d): %s\n",
                            argv0,
                            fsobj_path(dst),
                            src->stat.st_gid,
                            strerror(errno));
                    rc = -1;
                    goto End;
                }
            }
            rc = 1;
        }
    }

 End:
    return rc;
}

int
mode_clone(FSOBJ *src,
           FSOBJ *dst) {
    int rc = 0;


    if (f_force || (src->stat.st_mode&ALLPERMS) != (dst->stat.st_mode&ALLPERMS)) {
        if (!f_dryrun) {
            if (fchmodat(dst->parent->fd, dst->name, (src->stat.st_mode&ALLPERMS), AT_SYMLINK_NOFOLLOW) < 0) {
                if (errno == ENOTSUP && S_ISLNK(dst->stat.st_mode)) {
                    /* Linux doesn't support changing permissions on symbolic links */
                    rc = 0;
                    goto End;
                }

                fprintf(stderr, "%s: Error: %s: fchmodat(0%o): %s\n",
                        argv0,
                        fsobj_path(dst),
                        (src->stat.st_mode&ALLPERMS),
                        strerror(errno));
                rc = -1;
                goto End;
            }
        }
        rc = 1;
    }

 End:
    return rc;
}


acl_t
acl_get(FSOBJ *op,
        acl_type_t t) {
#if HAVE_ACL_GET_FD_NP
    if (op->fd >= 0)
        return acl_get_fd_np(op->fd, t);
#endif
#if HAVE_ACL_GET_LINK_NP
    return acl_get_link_np(fsobj_path(op), t);
#else
# if HAVE_ACL_GET_FD
    if (op->fd >= 0 && (op->flags & O_PATH) == 0 && t == ACL_TYPE_ACCESS)
        return acl_get_fd(op->fd);
# endif
# if HAVE_ACL_GET_FILE
    return acl_get_file(fsobj_path(op), t);
# else
    errno = ENOSYS;
    return NULL;
# endif
#endif
}


int
acl_set(FSOBJ *op,
        acl_t a,
        acl_type_t t) {
#if HAVE_ACL_SET_FD_NP
    if (op->fd >= 0)
        return acl_set_fd_np(op->fd, a, t);
#endif
#if HAVE_ACL_SET_LINK_NP
    return acl_set_link_np(fsobj_path(op), t, a);
#else
# if HAVE_ACL_SET_FD
    if (op->fd >= 0 && (op->flags & O_PATH) == 0 && t == ACL_TYPE_ACCESS)
        return acl_set_fd(op->fd, a);
# endif
# if HAVE_ACL_SET_FILE
    return acl_set_file(fsobj_path(op), t, a);
# else
    errno = ENOSYS;
    return NULL;
# endif
#endif
}

int
acls_clone(FSOBJ *src,
           FSOBJ *dst) {
    int rc = 0;
    acl_t s_acl = NULL, d_acl = NULL;
    acl_type_t s_t, d_t;

#ifdef ACL_TYPE_NFS4
    s_acl = acl_get(src, s_t = ACL_TYPE_NFS4);
#endif
#ifdef ACL_TYPE_ACCESS
    if (!s_acl)
        s_acl = acl_get(src, s_t = ACL_TYPE_ACCESS);
#endif

#ifdef ACL_TYPE_NFS4
    d_acl = acl_get(dst, d_t = ACL_TYPE_NFS4);
    if (!d_acl)
#endif
#ifdef ACL_TYPE_ACCESS
    if (!d_acl)
      d_acl = acl_get(dst, d_t = ACL_TYPE_ACCESS);
#endif

    if (!s_acl && !d_acl)
        return 0;

    if (f_force || (!s_acl && d_acl) || (s_acl && !d_acl) || (s_acl && (rc = acl_diff(s_acl, d_acl)))) {
        if (!s_acl) {
            fprintf(stderr, "No source ACL, generating trivial ACL\n");

            /* Generate a trivial ACL from the mode bits */
            s_acl = acl_init(0);
            /* XXX: ToDO */
            abort();
        }

        if (!f_dryrun) {
            if (acl_set(dst, s_acl, s_t) < 0) {
                fprintf(stderr, "%s: Error: %s: acl_set(fd=%d): %s\n",
                        argv0, fsobj_path(dst),
                        dst->fd, strerror(errno));
                rc = -1;
                goto End;
            }
        }
        rc = 1;
    }

#if defined(ACL_TYPE_ACCESS) && defined(ACL_TYPE_DEFAULT)
    if (d_t == ACL_TYPE_ACCESS && S_ISDIR(dst->stat.st_mode)) {
        acl_free(s_acl);
        acl_free(d_acl);

        s_acl = acl_get(src, s_t = ACL_TYPE_DEFAULT);
        d_acl = acl_get(dst, d_t = ACL_TYPE_DEFAULT);

        if (f_force || (!s_acl && d_acl) || (s_acl && !d_acl) || (s_acl && (rc = acl_diff(s_acl, d_acl)))) {
            if (!s_acl) {
                /* Generate a trivial ACL from the mode bits */
                s_acl = acl_init(0);
                /* XXX: ToDO */
                abort();
            }
            if (!f_dryrun) {
                if (acl_set(dst, s_acl, s_t) < 0) {
                    fprintf(stderr, "%s: Error: %s: acl_set(fd=%d): %s\n",
                            argv0, fsobj_path(dst),
                            dst->fd, strerror(errno));
                    rc = -1;
                    goto End;
                }
            }
            rc = 1;
        }
    }
#endif
    
 End:
    if (s_acl)
        acl_free(s_acl);
    if (d_acl)
        acl_free(d_acl);

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

int
dir_list(FSOBJ *dirp,
         int level) {
    FSOBJ obj;
    int rc, n = 0, ns = 0;

    fsobj_reopen(dirp, O_RDONLY|O_DIRECTORY);

    fsobj_init(&obj);
    while ((rc = fsobj_readdir(dirp, &obj)) > 0) {
	fprintf(stderr, "%*s%3d. %s [%o]\n", level*2, "", ++n, fsobj_path(&obj), (obj.stat.st_mode&S_IFMT));
        if (fsobj_typeof(&obj) == S_IFDIR)
            ns += dir_list(&obj,level+1);
    }
    fsobj_fini(&obj);
    return n+ns;
}


int
times_clone(FSOBJ *src,
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
        rc = 1;
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
        rc = 1;
    }
#else
    abort();
    errno = ENOSYS;
    rc = -1;
#endif

 End:
    return rc;
}


char *
fsobj_typestr(FSOBJ *op) {
    if (!op)
        return "Null";
    if (op->magic != FSOBJ_MAGIC)
        return "Invalid";
    if (op->name == NULL)
        return "Init";

    switch (op->stat.st_mode & S_IFMT) {
    case S_IFDIR:
        return "Dir";
    case S_IFREG:
        return "File";
    case S_IFLNK:
        return "Symlink";
    default:
        return "?";
    }
}




int
clone(FSOBJ *src,
      FSOBJ *dst) {
    int rc = 0, s_type = -1, d_type = -1;
    int mdiff = 0;


    if (f_debug)
	fprintf(stderr, "*** clone: %s [%s] -> %s [%s]\n",
		fsobj_path(src), fsobj_typestr(src),
		fsobj_path(dst), fsobj_typestr(dst));

    if (fsobj_isopen(src) < 1) {
        fprintf(stderr, "%s: Error: %s: Source not opened\n",
                argv0, fsobj_path(src));
        return -1;
    }

    s_type = fsobj_typeof(src);
    d_type = fsobj_typeof(dst);

    ++n_scanned;

    if (d_type > 0 && d_type != s_type) {
        /* Destination is different type -> delete it */

        if (f_debug)
            fprintf(stderr, "*** clone: different destination type - deleting\n");

        if (d_type == S_IFDIR)
            dir_prune(dst);

        if (fsobj_delete(dst) < 0) {
            fprintf(stderr, "%s: Error: %s: Delete: %s\n",
                    argv0, fsobj_path(dst), strerror(errno));
            return -1;
        }

        if (f_verbose)
            printf("- %s\n", fsobj_path(dst));
        ++n_deleted;

        mdiff |= MD_DEL;
        d_type = 0;
    }

    /* Make sure source is open for reading if file or directory */
    if ((src->flags & O_PATH) != 0 && (s_type == S_IFDIR || s_type == S_IFREG)) {
        if (f_debug)
            fprintf(stderr, "*** clone: Reopening source for reading\n");

        if (fsobj_reopen(src, O_RDONLY) < 0) {
            fprintf(stderr, "%s: Error: %s: Unable to read\n",
                    argv0, fsobj_path(src));
            return -1;
        }
    }

    if (dst->fd < 0) {
        switch (s_type) {
        case S_IFDIR:
            if (f_debug)
                fprintf(stderr, "*** clone: Creating & Opening destination directory for reading\n");
            if (fsobj_mkdir(dst, src->stat.st_mode) < 0) {
                fprintf(stderr, "%s: Error: %s: Create(directory): %s\n",
                        argv0, fsobj_path(dst), strerror(errno));
                return -1;
            }
            break;

        case S_IFREG:
            if (f_debug)
                fprintf(stderr, "*** clone: Creating & Opening destination file for reading\n");
            if (fsobj_open(dst, dst->parent, src->name, O_CREAT|O_WRONLY, src->stat.st_mode) < 0) {
                fprintf(stderr, "%s: Error: %s/%s: Create(file): %s\n",
                        argv0, fsobj_path(dst->parent), src->name, strerror(errno));
                return -1;
            }
            break;
        }

        ++n_added;
        if (f_verbose)
            printf("+ %s\n", fsobj_path(dst));

        mdiff |= MD_NEW;
    } else {
        switch (s_type) {
        case S_IFDIR:
            if (f_debug)
                fprintf(stderr, "*** clone: Reopening destination directory for reading\n");
            fsobj_reopen(dst, O_RDONLY);
            break;

        case S_IFREG:
            if (f_debug)
                fprintf(stderr, "*** clone: Reopening destination file for reading\n");
            fsobj_reopen(dst, O_WRONLY);
            break;
        }
    }

    switch (s_type) {
    case S_IFDIR:
        FSOBJ s_obj, d_obj;
        int s_rc, d_rc;


        if (f_debug)
            fprintf(stderr, "*** clone: Doing subdirectory\n");

        fsobj_init(&s_obj);
        fsobj_init(&d_obj);

        if (f_prune) {
            if (f_debug)
                fprintf(stderr, "*** clone: Pruning destination: %s\n", fsobj_path(dst));

            fsobj_rewind(dst);
            while ((d_rc = fsobj_readdir(dst, &d_obj)) > 0) {
                int s_rc = fsobj_open(&s_obj, src, d_obj.name, O_PATH, 0);

                if (f_debug)
                    fprintf(stderr, "*** clone: Prune Checking %s: %d\n",
                            d_obj.name, s_rc);

                if (s_rc < 0) {
                    if (fsobj_typeof(&d_obj) == S_IFDIR) {
                        d_rc = dir_prune(&d_obj);
                        if (d_rc < 0) {
                            if (f_debug)
                                fprintf(stderr, "*** clone: Prune dir_prune(%s) -> %d\n",
                                        fsobj_path(&d_obj), d_rc);
                            return -1;
                        }
                    }

                    if (fsobj_delete(&d_obj) < 0) {
                        fprintf(stderr, "%s: Error: %s: Delete: %s\n",
                                argv0, fsobj_path(dst), strerror(errno));
                        return -1;
                    }
                    ++n_scanned;
                    ++n_deleted;
                    if (f_verbose)
                        printf("- %s\n", fsobj_path(&d_obj));
                }
                fsobj_reset(&s_obj);
                fsobj_reset(&d_obj);
            }
            fsobj_reset(&s_obj);
            fsobj_reset(&d_obj);
        }

        fsobj_rewind(src);
        while ((s_rc = fsobj_readdir(src, &s_obj)) > 0) {
            int d_rc = fsobj_open(&d_obj, dst, s_obj.name, O_PATH, s_obj.stat.st_mode);

            if (f_debug)
                fprintf(stderr, "*** clone: Clone Checking %s: %d\n", s_obj.name, d_rc);

            if (d_rc < 0) {
                if (f_debug)
                    fprintf(stderr, "*** clone: fsobj_open(%s/%s): rc=%d [DST]\n",
                            fsobj_path(dst), s_obj.name, d_rc);
                rc = -1;
                break;
            }

            s_rc = clone(&s_obj, &d_obj);
            if (s_rc < 0) {
                if (f_debug)
                    fprintf(stderr, "*** clone: clone(%s, %s) -> %d\n",
                            fsobj_path(&s_obj), fsobj_path(&d_obj), s_rc);
                rc = -1;
                break;
            }

            fsobj_reset(&s_obj);
            fsobj_reset(&d_obj);
        }

        fsobj_fini(&s_obj);
        fsobj_fini(&d_obj);
        if (rc < 0)
            return rc;
        break;

    case S_IFREG:
        if (f_debug)
            fprintf(stderr, "*** clone: Doing file\n");
        if (f_force || src->stat.st_size != dst->stat.st_size || ts_isless(&dst->stat.st_mtim, &src->stat.st_mtim)) {
            if (!f_dryrun) {
                rc = file_clone(src, dst);
                if (rc < 0)
                    return -1;
            }
            mdiff |= MD_DATA;
        }
        break;

    case S_IFLNK:
        if (f_debug)
            fprintf(stderr, "*** clone: Doing symlink\n");
        rc = symlink_clone(src, dst);
        if (rc < 0)
            return -1;
        if (rc > 0)
            mdiff |= MD_DATA;
    }

    if (f_owners) {
        rc = owner_clone(src, dst);
        if (rc < 0)
            return -1;
        if (rc > 0)
            mdiff |= MD_UID;
    }

    if (f_groups) {
        rc = group_clone(src, dst);
        if (rc < 0)
            return -1;
        if (rc > 0)
            mdiff |= MD_GID;
    }

    rc = mode_clone(src, dst);
    if (rc < 0)
        return -1;
    if (rc > 0)
        mdiff |= MD_MODE;

    if (f_acls) {
        rc = acls_clone(src, dst);
        if (rc < 0)
            return -1;
        if (rc > 0)
            mdiff |= MD_ACL;
    }

    if (f_xattrs) {
        rc = attrs_clone(src, dst);
        if (rc < 0)
            return -1;
        if (rc > 0)
            mdiff |= MD_ATTR;
    }

    if (f_times) {
        rc = times_clone(src, dst);
        if (rc < 0)
            return -1;
        if (rc > 0)
            mdiff |= MD_TIME;
    }

    if (f_verbose > 2 || (f_verbose && mdiff)) {
        if (mdiff & MD_NEW) {
        } else {
            if (mdiff & (MD_DATA|MD_TIME|MD_ATTR|MD_ACL|MD_MODE|MD_UID|MD_GID)) {
                putchar('!');
                ++n_updated;
            } else
                putchar(' ');
            if (f_verbose > 1)
                printf(" %s ->", fsobj_path(src));
            printf(" %s", fsobj_path(dst));
            if (f_verbose > 1)
                printf(" [%04x]", mdiff);
            putchar('\n');
        }
    }

    return rc;
}



int
main(int argc,
     char *argv[]) {
    int i, j, k, rc = 0;
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
                puts("\nAll options may optionally take a numeric argument.");
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

    if (f_verbose)
	printf("[%s - Copyright (c) Peter Eriksson <pen@lysator.liu.se>]\n",
	       PACKAGE_STRING);

    if (f_all) {
	f_owners  += f_all;
	f_groups  += f_all;
	f_recurse += f_all;
	f_times   += f_all;
	f_acls    += f_all;
	f_xattrs  += f_all;
    }

    if (f_groups && geteuid() != 0) {
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

    if (i >= argc) {
	fprintf(stderr, "%s: Error: Missing required <source> arguments\n",
		argv[0]);
        rc = 1;
        goto Fail;
    }

    if (fsobj_open(&root_srcdir, NULL, argv[i], O_RDONLY|O_DIRECTORY, 0) <= 0) {
	fprintf(stderr, "%s: Error: %s: Open(source): %s\n",
		argv[0], argv[i], strerror(errno));
        rc = 1;
        goto Fail;
    }

    if (++i >= argc) {
        if (f_debug) {
            int n;

            puts("Source:");
            n = dir_list(&root_srcdir, 1);
            printf("%d total objects\n", n);

            fsobj_fini(&root_srcdir);
            fsobj_fini(&root_dstdir);
            exit(0);
        }

	fprintf(stderr, "%s: Error: Missing required <destination> argument\n",
		argv[0]);
        rc = 1;
        goto Fail;
    }


    if (fsobj_open(&root_dstdir, NULL, argv[i], (f_exist ? 0 : O_CREAT)|O_RDONLY,
		   (root_srcdir.stat.st_mode&ALLPERMS)|(root_srcdir.stat.st_mode&S_IFMT)) <= 0) {
	fprintf(stderr, "%s: Error: %s: Open(destination): %s\n",
		argv[0], argv[i], strerror(errno));
        rc = 1;
        goto Fail;
    }


    time(&t0);

    rc = clone(&root_srcdir, &root_dstdir);
    if (rc)
	goto End;

 End:
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

 Fail:
    fsobj_fini(&root_srcdir);
    fsobj_fini(&root_dstdir);

    exit(rc);
}
