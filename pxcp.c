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

#include "acls.h"
#include "fsobj.h"
#include "digest.h"

#ifdef HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_SEC
#define st_mtim st_mtimespec
#endif

#ifndef MAP_NOCORE
#define MAP_NOCORE 0
#endif

int f_maxdepth = 0;
int f_debug = 0;
int f_help = 0;
int f_verbose = 0;
int f_noxdev = 0;
int f_dryrun = 0;
int f_list = 0;
int f_sync = 0;
int f_stats = 0;
int f_recurse = 0;
int f_exist = 0;
int f_metaonly = 0;
int f_warnings = 0;
int f_silent = 0;
int f_ignore = 0;
int f_times = 0;
int f_sizes = 0;
int f_timestamps = 0;
int f_modes = 0;
int f_owners = 0;
int f_groups = 0;
int f_checksum = 0;
int f_acls = 0;
int f_flags = 0;
int f_xattrs = 0;
int f_prune = 0;
int f_force = 0;
int f_mmap = 0;
int f_all = 0;

int my_groups = 0;
gid_t *my_groupv = NULL;

struct timespec t0, t1;


int
get_int(char *start,
          char **next) {
    return strtol(start, next, 0);
}

int
get_digest(char *start,
           char **next) {
    int v;

    v = digest_str2type(start);
    if (v < 0)
        v = get_int(start, next);
    else {
	while (*start)
	    ++start;
        
        *next = start;
    }
    
    return v;
}


struct options {
    char c;
    char *l;
    int *vp;
    int (*f)(char *start, char **next);
    char *help;
} options[] = {
    { 'a', "all",        &f_all,         get_int,     "Archive mode (enables c,g,m,o,r,t,A,F,T,X options)" },
    { 'd', "depth",      &f_maxdepth,    get_int,     "Max recursive depth" },
    { 'e', "exist",      &f_exist,       get_int,     "Only copy to existing targets" },
    { 'f', "force",      &f_force,       get_int,     "Force updates" },
    { 'g', "groups",     &f_groups,      get_int,     "Copy object group" },
    { 'h', "help",       &f_help,        get_int,     "Display usage information" },
    { 'i', "ignore",     &f_ignore,      get_int,     "Ignore non-fatal errors and continue" },
    { 'm', "modes",      &f_modes,       get_int,     "Copy mode bits" },
    { 'n', "dryrun",     &f_dryrun,      get_int,     "Enable dry-run mode" },
    { 'o', "owners",     &f_owners,      get_int,     "Copy object owner" },
    { 'p', "prune",      &f_prune,       get_int,     "Prune removed objects" },
    { 'r', "recurse",    &f_recurse,     get_int,     "Enable recursion" },
    { 's', "sizes",      &f_sizes,       get_int,     "Only copy files if sizes differ" },
    { 't', "times",      &f_times,       get_int,     "Only copy files if timestamps differ" },
    { 'u', "metaonly",   &f_metaonly,    get_int,     "Only copy metadata" },
    { 'v', "verbose",    &f_verbose,     get_int,     "Set verbosity level" },
    { 'w', "warnings",   &f_warnings,    get_int,     "Display warnings/notices" },
    { 'x', "xdev",       &f_noxdev,      get_int,     "Stay in same filesystem" },
    { 'A', "acls",       &f_acls,        get_int,     "Copy ACLs" },
    { 'C', "checksum",   &f_checksum,    get_digest,  "Only copy files if checksums differs" },
    { 'D', "debug",      &f_debug,       get_int,     "Set debugging level" },
    { 'F', "flags",      &f_flags,       get_int,     "Copy object flags" },
    { 'L', "list",       &f_list,        get_int,     "List objects" },
    { 'M', "mmap",       &f_mmap,        get_int,     "Use mmap(2)" },
    { 'S', "sync",       &f_sync,        get_int,     "Select file sync modes" },
    { 'T', "timestamps", &f_timestamps,  get_int,     "Copy timestamps" }, 
    { 'X', "xattrs",     &f_xattrs,      get_int,     "Copy Extended Attributes" },
    { -1,  NULL,         NULL,           NULL,        NULL }
};

char *argv0 = "pxcp";

FSOBJ src_base, dst_base;

unsigned long n_scanned = 0;
unsigned long n_added = 0;
unsigned long n_updated = 0;
unsigned long n_deleted = 0;

unsigned long long b_written = 0;

int cur_depth = 0;


#define MD_NEW    0x0001
#define MD_DEL    0x0002
#define MD_DATA   0x0010
#define MD_TIME   0x0020
#define MD_MODE   0x0100
#define MD_UID    0x0200
#define MD_GID    0x0400
#define MD_ACL    0x1000
#define MD_ATTR   0x2000
#define MD_FLAG   0x4000

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
ts_check(struct timespec *a,
         struct timespec *b,
         int exact_check) {
    int v;
    
    if (exact_check)
        v = !(a->tv_sec == b->tv_sec && a->tv_nsec == b->tv_nsec);
    else {
        if (a->tv_sec == b->tv_sec)
            v = a->tv_nsec < b->tv_nsec;
        else
            v = a->tv_sec < b->tv_sec;
    }

    if (f_debug > 1)
        fprintf(stderr, "** ts_check(%s): %ld.%09ld vs %ld.%09ld -> %d [f_times=%d]\n",
                exact_check ? "exact" : "newer",
                a->tv_sec, a->tv_nsec, b->tv_sec, b->tv_nsec, v, f_times);
    
    return v;
}



ssize_t
symlink_clone(FSOBJ *src,
              FSOBJ *dst) {
    char s_pbuf[PATH_MAX+1], d_pbuf[PATH_MAX+1];
    ssize_t s_plen, d_plen;
    int rc = 0;

    s_plen = fsobj_readlink(src, NULL, s_pbuf, sizeof(s_pbuf));
    if (s_plen < 0) {
        fprintf(stderr, "%s: Error: %s: Read(symlink): %s\n",
                argv0, fsobj_path(src), strerror(errno));
        return -1;
    }
    s_pbuf[s_plen] = '\0';

    d_plen = fsobj_readlink(dst, NULL, d_pbuf, sizeof(d_pbuf));
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
            if (d_plen >= 0 && fsobj_delete(dst, NULL) < 0) {
                fprintf(stderr, "%s: Error: %s: Delete(symlink): %s\n",
                        argv0, fsobj_path(dst), strerror(errno));
		return -1;
	    }
            
	    if (fsobj_symlink(dst, NULL, s_pbuf) < 0) {
		fprintf(stderr, "%s: Error: %s -> %s: Create(symlink): %s\n",
			argv0, fsobj_path(dst), s_pbuf, strerror(errno));
		return -1;
	    }
	}
	rc = 1;
    }

    if (fsobj_reopen(dst, O_RDONLY) < 0) {
        fprintf(stderr, "%s: Error: %s: Stat(symlink): %s\n",
                argv0, fsobj_path(dst), strerror(errno));
        return -1;
    }

    return rc;
}




ssize_t
file_clone(FSOBJ *src,
           FSOBJ *dst) {
    ssize_t wr, rr, rc = 0;
    unsigned char *s_bufp = NULL;
    unsigned char *d_bufp = NULL;
    int tfd = -1;
    char tmppath[PATH_MAX], *tmpname = NULL;
    int update_f = f_force;


    if (!f_sizes && !f_times && !f_checksum && !f_metaonly)
        update_f = 1;

    if (!update_f && fsobj_typeof(dst) == 0)
        update_f = 1;
    
    if (!update_f && f_sizes && src->stat.st_size != dst->stat.st_size) {
        if (f_debug > 1)
            fprintf(stderr, "** file_clone(%s, %s): Sizes differs\n",
                    fsobj_path(src), fsobj_path(dst));
        update_f = 1;
    }

    if (!update_f && f_times && ts_check(&dst->stat.st_mtim, &src->stat.st_mtim, f_times-1)) {
        if (f_debug > 1)
            fprintf(stderr, "** file_clone(%s, %s): Times differs\n",
                    fsobj_path(src), fsobj_path(dst));
        update_f = 1;
    }

    /* Make sure object is opened for reading */
    if (fsobj_reopen(src, O_RDONLY|O_DIRECT) < 0) {
        fprintf(stderr, "%s: Error: %s: Reopening): %s\n",
                argv0, fsobj_path(src), strerror(errno));
        return -1;
    }
        
    if (!update_f && f_checksum) {
        DIGEST s_digest, d_digest;
        unsigned char s_digbuf[DIGEST_BUFSIZE_MAX];
        unsigned char d_digbuf[DIGEST_BUFSIZE_MAX];
        ssize_t s_rc = -1, d_rc = -1;
        

        if (!f_mmap)
            f_mmap = 1;
        
        digest_init(&s_digest, f_checksum);
	if (fsobj_mmap(src, (void **) &s_bufp) > 0)
	    digest_update(&s_digest, s_bufp, src->stat.st_size);
	s_rc = digest_final(&s_digest, s_digbuf, sizeof(s_digbuf));
        
        /* Make sure object is opened for reading */
        if (fsobj_reopen(dst, O_RDONLY|O_DIRECT) < 0) {
            fprintf(stderr, "%s: Error: %s: Reopening): %s\n",
                    argv0, fsobj_path(dst), strerror(errno));
	    rc = -1;
	    goto End;
        }

        digest_init(&d_digest, f_checksum); 
	if (fsobj_mmap(dst, (void **) &d_bufp) > 0) {
	    digest_update(&d_digest, d_bufp, dst->stat.st_size);
	    fsobj_munmap(dst, d_bufp);
	    d_bufp = NULL;
	}
        d_rc = digest_final(&d_digest, d_digbuf, sizeof(d_digbuf));

        if (s_rc >= 0 && s_rc == d_rc && memcmp(s_digbuf, d_digbuf, s_rc) != 0) {
            if (f_debug > 1)
                fprintf(stderr, "** file_clone(%s, %s): Digest differs\n",
                        fsobj_path(src), fsobj_path(dst));
            update_f = 1;
        } else {
            if (f_debug > 1)
                fprintf(stderr, "** file_clone(%s, %s): Digest equal\n",
                        fsobj_path(src), fsobj_path(dst));
	}
    }

    if (f_dryrun || !update_f) {
        rc = 0;
	goto End;
    }
    
#if defined(HAVE_MKOSTEMPSAT)
    strcpy(tmppath, ".pxcp_tmpfile.XXXXXX");
    tmpname = tmppath;
    {
        int f = f_sync ? O_SYNC|(f_sync > 1 ? O_DIRECT : 0) : 0;
        tfd = mkostempsat(dst->parent->fd, tmpname, 0, f);
        if (f_debug > 1)
            fprintf(stderr, "** file_clone(%s,%s): mkostempat(%d, %s, 0, 0x%x [%s]) -> %d (%s)\n",
                    fsobj_path(src), fsobj_path(dst),
                    dst->parent->fd, tmppath, f, _fsobj_open_flags(f),
                    tfd, tfd < 0 ? strerror(errno) : "");
    }
#elif defined(HAVE_MKOSTEMP)
    sprintf(tmppath, "%s/.pxcp_tmpfile.XXXXXX", fsobj_path(dst->parent));
    tmpname = strrchr(tmppath, '/');
    if (tmpname)
        ++tmpname;
        
    tfd = mkostemp(tmppath, 0);
    if (f_debug > 1)
        fprintf(stderr, "** file_clone(%s,%s): mkostemp(%s, 0) -> %d (%s)\n",
                fsobj_path(src), fsobj_path(dst),
                tmppath,
                tfd, tfd < 0 ? strerror(errno) : "");
#elif defined(HAVE_MKSTEMP)
    sprintf(tmppath, "%s/.pxcp_tmpfile.XXXXXX", fsobj_path(dst->parent));
    tmpname = strrchr(tmppath, '/');
    if (tmpname)
        ++tmpname;
    tfd = mkstemp(tmppath);
    if (f_debug > 1)
        fprintf(stderr, "** file_clone(%s,%s): mkstemp(%s) -> %d (%s)\n",
                fsobj_path(src), fsobj_path(dst),
                tmppath,
                tfd, tfd < 0 ? strerror(errno) : "");
#else
    int n = 0;
    do {
        int f = O_CREAT|O_WRONLY|O_EXCL|(f_sync ? O_SYNC|(f_sync > 1 ? O_DIRECT : 0) : 0);
            
        sprintf(tmppath, ".pxcp_tmpfile.%d.%d", getpid(), n++);
        tfd = openat(dst->parent->fd, tmpname, f, 0666);
    } while (tfd < 0 && errno == EEXIST && n < 5);
        
    if (f_debug > 1) {
        fprintf(stderr, "** file_clone(%s,%s): openat(%d,%s,0x%x[%s],0400) -> %d (%s)\n",
                fsobj_path(src), fsobj_path(dst), 
                dst->parent->fd, tmpname, f, _fsobj_open_flags(f),
                tfd, tfd < 0 ? strerror(errno) : "");
        return -1;
    }
        
    tmpname = tmppath;
#endif
        
    if (tfd < 0) {
        fprintf(stderr, "%s: Error: %s/%s: Create(tmpfile): %s\n",
                argv0, fsobj_path(dst->parent), tmpname, strerror(errno));
        return -1;
    }
        
        
    if (f_mmap) {
        if (src->stat.st_size > 0) {
            if (s_bufp == NULL) {
	        if (fsobj_mmap(src, (void **) &s_bufp) < 0) {
		    fprintf(stderr, "%s: Error: %s: mmap: %s\n",
                            argv0, fsobj_path(src), strerror(errno));
                    rc = -1;
                    goto End;
                }
            }
                
            if (tfd >= 0) {
                wr = write(tfd, s_bufp, src->stat.st_size);
                if (wr < 0) {
                    int t_errno = errno;
                        
                    fprintf(stderr, "%s: Error: %s/%s: Write(%d,%p,%lld): %s\n",
                            argv0, fsobj_path(dst->parent), tmpname,
                            tfd, s_bufp, (long long int) src->stat.st_size,
                            strerror(errno));
                    errno = t_errno;
                    rc = -1;
                    goto End;
                }
                    
                b_written += wr;
                    
                if (wr != src->stat.st_size) {
                    fprintf(stderr, "%s: Error: %s/%s: Short write\n",
                            argv0, fsobj_path(dst->parent), tmpname);
                    errno = EPIPE;
                    rc = -1;
                    goto End;
                }

                rc = 1;
            }
        }
    } else {
        char buf[256*1024];
            
        if (ftruncate(tfd, src->stat.st_size) < 0) {
            int t_errno = errno;
                
            fprintf(stderr, "%s: Error: %s/%s: Ftruncate: %s\n",
                    argv0, fsobj_path(dst->parent), tmpname, strerror(errno));
            errno = t_errno;
            rc = -1;
            goto End;
        }
            
        while ((rr = read(src->fd, buf, sizeof(buf))) > 0) {
            wr = write(tfd, buf, rr);
            if (wr < 0) {
                int t_errno = errno;
                    
                fprintf(stderr, "%s: Error: %s/%s: Write(%d,%p,%lld): %s\n",
                        argv0, fsobj_path(dst->parent), tmpname,
                        tfd, buf, (long long int) rr,
                        strerror(errno));
                    
                errno = t_errno;
                rc = -1;
                goto End;
            }
                
            b_written += wr;
                
            if (wr != rr) {
                fprintf(stderr, "%s: Error: %s/%s: Short write\n",
                        argv0, fsobj_path(dst->parent), tmpname);
                errno = EPIPE;
                rc = -1;
                goto End;
            }
                
        }
        if (rr < 0) {
            int t_errno = errno;
                
            fprintf(stderr, "%s: Error: %s/%s: Read: %s\n",
                    argv0, fsobj_path(dst->parent), tmpname, strerror(errno));
            errno = t_errno;
            rc = -1;
            goto End;
        }

        rc = 1;
    }
        
    if (dst->fd >= 0) {
        int d_rc;
            
        d_rc = dup2(tfd, dst->fd);
        if (f_debug)
            fprintf(stderr, "** file_clone: dup2(%d, %d) -> %d\n",
                    tfd, dst->fd, d_rc);
        close(tfd);
    } else {
        dst->fd = tfd;
        if (f_debug)
            fprintf(stderr, "** file_clone: op->fd = %d\n",
                    tfd);
    }
        
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

    if (fsobj_reopen(dst, O_RDONLY) < 0) {
        int t_errno = errno;
            
        fprintf(stderr, "%s: Error: %s: Reopening: %s\n",
                argv0, fsobj_path(dst), strerror(errno));
        errno = t_errno;
        rc = -1;
        goto End;
    }

 End:
    if (s_bufp)
        fsobj_munmap(src, s_bufp);
    if (d_bufp)
        fsobj_munmap(dst, d_bufp);
    if (tmpname)
        (void) unlinkat(dst->parent->fd, tmpname, 0);

    return rc <= 0 ? rc : 1;
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
    if (fsobj_reopen(dp, O_RDONLY|O_DIRECTORY) < 0)
      return -1;


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

	    if (fsobj_equal(&d_obj, &src_base)) {
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
            if (fsobj_delete(&d_obj, NULL) < 0) {
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
	        int f_rc = fsobj_chown(dst, NULL, src->stat.st_uid, -1);
		if (f_rc < 0) {
                    fprintf(stderr, "%s: Error: %s: Change Owner (uid=%d): %s\n",
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
	        int f_rc = fsobj_chown(dst, NULL, src->stat.st_uid, -1);
		if (f_rc < 0) {
		  fprintf(stderr, "%s: Error: %s: Change Group (gid=%d): %s\n",
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
modes_clone(FSOBJ *src,
	    FSOBJ *dst) {
    if (f_dryrun)
        return 0;

    
    if ((src->stat.st_mode&ALLPERMS) == 0) {
      /* If all bits off -> Solaris with ACLs? */
      if (f_debug)
	  fprintf(stderr, "** mode_clone: all-zero perms, not copying\n");
      return 0;
    }
    
    if (f_force || (src->stat.st_mode&ALLPERMS) != (dst->stat.st_mode&ALLPERMS)) {
        if (fsobj_chmod(dst, NULL, (src->stat.st_mode&ALLPERMS)) < 0) {
            if (errno == EOPNOTSUPP && S_ISLNK(dst->stat.st_mode))
                /* Linux doesn't support changing permissions on symbolic links */
                return 0;
      
            fprintf(stderr, "%s: Error: %s: Setting Mode Bits(0%o <- 0%o): %s\n",
                    argv0,
                    fsobj_path(dst),
                    (dst->stat.st_mode&ALLPERMS),
                    (src->stat.st_mode&ALLPERMS),
                    strerror(errno));
            return -1;
        }
    }

    return 0;
}


int
flags_clone(FSOBJ *src,
            FSOBJ *dst) {
    if (f_dryrun)
        return 0;

#if HAVE_STRUCT_STAT_ST_FLAGS
    if (f_force || src->stat.st_flags != dst->stat.st_flags) {
        if (fsobj_chflags(dst, NULL, src->stat.st_flags) < 0) {
            fprintf(stderr, "%s: Error: %s: Setting Flag Bits(0x%x <- 0x%x): %s\n",
                    argv0,
                    fsobj_path(dst),
                    dst->stat.st_flags,
                    src->stat.st_flags,
                    strerror(errno));
            return -1;
        }
    }
#endif
    
    return 0;
}



int
acls_clone(FSOBJ *src,
           FSOBJ *dst) {
    int rc = 0, s_rc = -1, d_rc = -1;
    GACL s_acl, d_acl;

    
#ifdef ACL_TYPE_NFS4
    s_rc = gacl_get(&s_acl, src, ACL_TYPE_NFS4);
#endif
#ifdef ACL_TYPE_ACCESS
    if (s_rc < 0)
        s_rc = gacl_get(&s_acl, src, ACL_TYPE_ACCESS);
#endif

#ifdef ACL_TYPE_NFS4
    d_rc = gacl_get(&d_acl, dst, ACL_TYPE_NFS4);
#endif
#ifdef ACL_TYPE_ACCESS
    if (d_rc < 0)
      d_rc = gacl_get(&d_acl, dst, ACL_TYPE_ACCESS);
#endif

    /* No ACL on source object */
    if (s_rc < 0)
        return 0;

    if (f_force || (s_rc == 0 && d_rc > 0) || (s_rc > 0 && d_rc == 0) || (s_rc > 0 && (rc = gacl_diff(&s_acl, &d_acl)))) {
        if (!f_dryrun) {
            if (gacl_set(dst, &s_acl) < 0) {
                fprintf(stderr, "%s: Error: %s: Setting ACL: %s\n",
                        argv0, fsobj_path(dst),
                        strerror(errno));
                rc = -1;
                goto End;
            }
        }
        rc = 1;
    }

#if defined(ACL_TYPE_ACCESS) && defined(ACL_TYPE_DEFAULT)
    if (s_rc > 0 && s_acl.t == ACL_TYPE_ACCESS && S_ISDIR(dst->stat.st_mode)) {
        gacl_free(&s_acl);
        gacl_free(&d_acl);

        s_rc = gacl_get(&s_acl, src, ACL_TYPE_DEFAULT);
        d_rc = gacl_get(&d_acl, dst, ACL_TYPE_DEFAULT);

        if (f_force || (s_rc < 0 && d_rc >= 0) || (s_rc >= 0 && d_rc < 0) || (s_rc >= 0 && (rc = gacl_diff(&s_acl, &d_acl)))) {
            if (s_rc < 0) {
                /* Generate a trivial ACL from the mode bits */
                /* XXX: ToDO */
                abort();
            }
            if (!f_dryrun) {
                if (gacl_set(dst, &s_acl) < 0) {
                    fprintf(stderr, "%s: Error: %s: Setting Default ACL: %s\n",
                            argv0, fsobj_path(dst),
                            strerror(errno));
                    rc = -1;
                    goto End;
                }
            }
            rc = 1;
        }
    }
#endif

 End:
    if (s_rc > 0)
	gacl_free(&s_acl);
    if (d_rc > 0)
	gacl_free(&d_acl);
    return rc;
}

/* Clone extended attributes */
int
attrs_clone(FSOBJ *src,
	    FSOBJ *dst) {
    ssize_t s_alen, d_alen = 0;
    char *s_alist = NULL, *d_alist = NULL;
    int rc = 0, s_i, d_i = 0;


    if (f_debug)
        fprintf(stderr, "*** attrs_clone(%s, %s)\n",
                fsobj_path(src), fsobj_path(dst));
    
    /* Get list of extended attribute from source */
    s_alen = fsobj_list_attrs(src, NULL, NULL, 0);
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

    s_alen = fsobj_list_attrs(src, NULL, s_alist, s_alen);
    if (s_alen < 0) {
	fprintf(stderr, "%s: Error: %s: extattr_list_fd: %s\n",
		argv0, fsobj_path(src), strerror(errno));
	return -1;
    }


    /* Get list of Attribute from destination */
    d_alen = fsobj_list_attrs(dst, NULL, NULL, 0);
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
    
    d_alen = fsobj_list_attrs(dst, NULL, d_alist, d_alen);
    if (d_alen < 0) {
	fprintf(stderr, "%s: Error: %s: extattr_list_fd: %s\n",
		argv0, fsobj_path(dst), strerror(errno));
	return -1;
    }

    
    /* Scan for attributes to remove */
    d_i = 0;
    while (d_i < d_alen) {
	/* Scan for matching attribute */
	char *aname = d_alist+d_i;

	if (f_debug)
	    fprintf(stderr, "** attrs_clone: check for remove : aname = %s\n", aname);
	
	s_i = 0;
	while (s_i < s_alen) {
	    if (strcmp(aname, d_alist+d_i) == 0)
		break; /* Found a match */
	    
	    /* Locate end of SRC aname */
	    while (s_i < s_alen && s_alist[s_i++] != '\0')
		;
	}
	if (s_i >= s_alen) {
	    if (!f_dryrun) {
		if (fsobj_delete_attr(dst, NULL, aname) < 0) {
		    fprintf(stderr, "%s: Error: %s: %s: Delete(attribute): %s\n",
			    argv0, fsobj_path(dst), aname, strerror(errno));
		    rc = -1;
		    goto End;
		}
	    }
	    if (f_verbose > 1)
		printf("  - %s : %s\n", fsobj_path(dst), aname);
	    rc = 1;
	}
	
	/* Locate end of DST aname */
	while (d_i < d_alen && d_alist[d_i++] != '\0')
	    ;
    }

    s_i = 0;
    while (s_i < s_alen) {
	/* Scan for matching attribute */
	char *aname = s_alist+s_i;	
	ssize_t s_adlen, d_adlen;
	void *s_adata, *d_adata;
	
	if (f_debug)
	    fprintf(stderr, "** attrs_clone: check for copy : Aname = %s\n", aname);
        
	d_i = 0;
	while (d_i < d_alen) {
	    if (strcmp(aname, d_alist+d_i) == 0)
		break;
	    
	    /* Locate end of DST aname */
	    while (d_i < d_alen && d_alist[d_i++] != '\0')
		;
	}
	
	/* Get SRC attribute */
	s_adlen = fsobj_get_attr(src, NULL, aname, NULL, 0);
	if (s_adlen < 0) {
	    fprintf(stderr, "%s: Error: %s: %s: extattr_get_fd: %s\n",
		    argv0, fsobj_path(src), aname, strerror(errno));
	    rc = -1;
	    goto End;
	}

	s_adata = malloc(s_adlen);
	if (!s_adata) {
	    fprintf(stderr, "%s: Error: %s: %s: malloc(%ld): %s\n",
		    argv0, fsobj_path(src), aname, s_adlen, strerror(errno));
	    rc = -1;
	    goto End;
	}

	s_adlen = fsobj_get_attr(src, NULL, aname, s_adata, s_adlen);
	if (s_adlen < 0) {
	    fprintf(stderr, "%s: Error: %s: %s: extattr_get_fd: %s\n",
		    argv0, fsobj_path(src), aname, strerror(errno));
	    rc = -1;
	    goto End;
	}

	/* Get DST attribute */
	d_adlen = fsobj_get_attr(dst, NULL, aname, NULL, 0);
	if (d_adlen < 0) {
	    fprintf(stderr, "%s: Error: %s: %s: extattr_get_fd: %s\n",
		    argv0, fsobj_path(dst), d_alist+d_i, strerror(errno));
	    rc = -1;
	    goto End;
	}

	/* Only bother to read and check content if same lenght */
	if (s_adlen == d_adlen) {
	    d_adata = malloc(d_adlen);
	    if (!d_adata) {
		fprintf(stderr, "%s: Error: %s: %s: malloc(%ld): %s\n",
			argv0, fsobj_path(dst), aname, d_adlen, strerror(errno));
		rc = -1;
		goto End;
	    }
		
	    d_adlen = fsobj_get_attr(dst, NULL, aname, d_adata, d_adlen);
	    if (d_adlen < 0) {
		fprintf(stderr, "%s: Error: %s: %s: extattr_get_fd: %s\n",
			argv0, fsobj_path(dst), aname, strerror(errno));
		rc = -1;
		goto End;
	    }
	    
	    if (memcmp(d_adata, s_adata, s_adlen) != 0) {
		if (!f_dryrun) {
		    if (fsobj_set_attr(dst, NULL, aname, s_adata, s_adlen) < 0) {
			fprintf(stderr, "%s: Error: %s: %s: extattr_set_fd: %s\n",
				argv0, fsobj_path(dst), aname, strerror(errno));
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
		if (fsobj_set_attr(dst, NULL, aname, s_adata, s_adlen) < 0) {
		    fprintf(stderr, "%s: Error: %s: %s: extattr_set_fd: %s\n",
			    argv0, fsobj_path(dst), aname, strerror(errno));
		    rc = -1;
		    goto End;
		}
	    }
	    if (f_verbose > 1)
		printf("  ! %s : %s\n", fsobj_path(dst), aname);
	    rc = 1;
	}

	free(s_adata);
	s_adata = NULL;
	
	/* Locate end of SRC aname */
	while (s_i < s_alen && s_alist[s_i++] != '\0')
	    ;
    }

 End:
    if (s_alist)
	free(s_alist);
    if (d_alist)
	free(d_alist);

    return rc;
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

static void
p_buf(FILE *fp,
      const uint8_t *buf,
      size_t len) {
    putc('[', fp);
    while (len-- > 0) {
        fprintf(fp, "%02x", *buf++);
        if (len > 0)
            putc(' ', fp);
    }
    putc(']', fp);
}

int
dir_list(FSOBJ *op,
         int level) {
    FSOBJ obj;
    int n = 1, rc;

    
    if (f_debug > 1) 
	fprintf(stderr, "** dir_list(%s)\n", fsobj_path(op));
    
    printf("%*s%s",
	   level*2, "", fsobj_path(op));

    if (f_checksum && S_ISREG(op->stat.st_mode)) {
        uint8_t digest[DIGEST_BUFSIZE_MAX];
        ssize_t len;
        
        len = fsobj_digest(op, f_checksum, digest, sizeof(digest));
        if (len >= 0) {
            putchar('\t');
            p_buf(stdout, digest, len);
        }
    }

    if (f_verbose)
        printf("\t{t=%s, p=%04o, s=%llu, u=%d, g=%d}",
               _fsobj_mode_type2str(op->stat.st_mode),
               op->stat.st_mode&ALLPERMS,
               (long long unsigned) op->stat.st_size,
               op->stat.st_uid,
               op->stat.st_gid);
        
    putchar('\n');
    
    if (!f_recurse || fsobj_typeof(op) != S_IFDIR || (f_maxdepth && level > f_maxdepth))
	return 1;
    
    if (fsobj_reopen(op, O_RDONLY|O_DIRECTORY) < 0)
	return -1;
	
    fsobj_init(&obj);
    while ((rc = fsobj_readdir(op, &obj)) > 0) {
	rc = dir_list(&obj, level+1);
	fsobj_reset(&obj);
	if (rc < 0)
	    break;
	n += rc;
    }
    fsobj_fini(&obj);
    if (rc < 0)
	return -1;
    
    return n;
}


int
times_clone(FSOBJ *src,
            FSOBJ *dst) {
    int rc = 0;

#if HAVE_STRUCT_STAT_ST_BIRTHTIM_TV_SEC
    if (f_force || ts_check(&dst->stat.st_birthtim, &src->stat.st_birthtim, 1)) {
	if (!f_dryrun) {
	    struct timespec times[2];

	    times[0].tv_nsec = UTIME_OMIT;
	    times[1] = src->stat.st_birthtim;

	    if (fsobj_utimens(dst, NULL, times) < 0) {
		fprintf(stderr, "%s: Error: %s: Reset Birth Time: %s\n",
			argv0, fsobj_path(dst), strerror(errno));
		rc = -1;
		goto End;
	    }
	}
        rc = 1;
    }
#endif
    if (f_force || ts_check(&dst->stat.st_mtim, &src->stat.st_mtim, 1)) {
	if (!f_dryrun) {
	    struct timespec times[2];

	    times[0].tv_nsec = UTIME_OMIT;
	    times[1] = src->stat.st_mtim;

	    if (fsobj_utimens(dst, NULL, times) < 0) {
		fprintf(stderr, "%s: Error: %s: Reset Modification Time: %s\n",
			argv0, fsobj_path(dst), strerror(errno));
		rc = -1;
		goto End;
	    }
	}
        rc = 1;
    }

 End:
    return rc;
}





/*
 * Clone an object
 */
int
clone(FSOBJ *src,
      FSOBJ *dst,
      int level) {
    int rc = 0, s_type = -1, d_type = -1;
    int mdiff = 0;
    FSOBJ s_obj, d_obj;
    int s_rc, d_rc;




    if (f_debug)
	fprintf(stderr, "*** clone: %s [%s] -> %s [%s]\n",
		fsobj_path(src), fsobj_typestr(src),
		fsobj_path(dst), fsobj_typestr(dst));

    s_type = fsobj_typeof(src);
    d_type = fsobj_typeof(dst);

    ++n_scanned;

    if (d_type > 0 && d_type != s_type) {
        /* Destination is different type -> delete it */

        if (!f_prune) {
            fprintf(stderr, "%s: Error: %s -> %s: Destination object is different type - not copying\n",
                    argv0, fsobj_path(src), fsobj_path(dst));
            return -1;
        }
        
        if (f_debug)
            fprintf(stderr, "*** clone: different destination type - deleting\n");

        if (d_type == S_IFDIR)
            dir_prune(dst);

        if (fsobj_delete(dst, NULL) < 0) {
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
            fprintf(stderr, "%s: Error: %s: Open: %s\n",
                    argv0, fsobj_path(src), strerror(errno));
            return -1;
        }
    }

    if (dst->fd < 0) {
        if (s_type == S_IFDIR) {
#if 1
	    mode_t mode = 0777;
#else
	    mode_t mode = src->stat.st_mode&ALLPERMS;
	    if (!mode) {
	      fprintf(stderr, "*** clone: Forcing directory mode to 0700\n");
	        mode = 0700;
	    }
#endif
	    
            if (f_debug)
	      fprintf(stderr, "*** clone: Creating & Opening destination directory for reading (mode=%04o)\n", mode);
            if (fsobj_open(dst, NULL, NULL, O_CREAT|O_RDONLY|O_DIRECTORY, mode|S_IFDIR) < 0) {
                fprintf(stderr, "%s: Error: %s: Create(directory): %s\n",
                        argv0, fsobj_path(dst), strerror(errno));
                return -1;
            }
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
        if (f_maxdepth && level > f_maxdepth) {
            if (f_debug)
                fprintf(stderr, "*** clone: Maxdepth (%d) reached, not doing subdirectory\n",
                        f_maxdepth);
            break;
	}
        
        if (f_debug)
            fprintf(stderr, "*** clone: Doing subdirectory [level=%d, maxdepth=%d]\n",
		    level, f_maxdepth);
 
	fsobj_init(&s_obj);
        fsobj_init(&d_obj);

        if (f_prune) {
            if (f_debug)
                fprintf(stderr, "*** clone: Pruning destination: %s\n", fsobj_path(dst));

            fsobj_rewind(dst);
            while ((d_rc = fsobj_readdir(dst, &d_obj)) > 0) {
                int s_rc;


		if (d_obj.stat.st_dev == src_base.stat.st_dev &&
		    d_obj.stat.st_ino == src_base.stat.st_ino) {
		    fprintf(stderr, "%s: Error: %s: Passing Source Base\n",
			    argv0, fsobj_path(&d_obj));
		}
			
                s_rc = fsobj_open(&s_obj, src, d_obj.name, O_PATH, 0);
                if (f_debug)
                    fprintf(stderr, "*** clone: Prune Checking %s: %d\n",
                            d_obj.name, s_rc);
                if (s_rc == 0) {
                    if (fsobj_typeof(&d_obj) == S_IFDIR) {
                        d_rc = dir_prune(&d_obj);
                        if (d_rc < 0) {
                            if (f_debug)
                                fprintf(stderr, "*** clone: Prune dir_prune(%s) -> %d\n",
                                        fsobj_path(&d_obj), d_rc);
                            return -1;
                        }
                    }

                    if (fsobj_delete(&d_obj, NULL) < 0) {
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
        }

	if (f_recurse) {
	    fsobj_rewind(src);
	    while ((s_rc = fsobj_readdir(src, &s_obj)) > 0) {
		int d_rc;

		if (s_obj.stat.st_dev == dst_base.stat.st_dev &&
		    s_obj.stat.st_ino == dst_base.stat.st_ino) {
		    fprintf(stderr, "%s: Error: %s: Infinte recursion - skipping\n",
			    argv0, fsobj_path(&s_obj));
		    continue;
		}
		
		d_rc = fsobj_open(&d_obj, dst, s_obj.name, O_PATH, s_obj.stat.st_mode);
		if (f_debug)
		    fprintf(stderr, "*** clone: Clone Checking %s: %d\n", s_obj.name, d_rc);
		if (d_rc < 0) {
		    if (f_debug)
			fprintf(stderr, "*** clone: fsobj_open(%s/%s): rc=%d [DST]\n",
				fsobj_path(dst), s_obj.name, d_rc);
		    rc = -1;
		    break;
		}
		
		s_rc = clone(&s_obj, &d_obj, level+1);
		if (s_rc < 0) {
		    if (f_debug)
			fprintf(stderr, "*** clone: clone(%s, %s) -> %d\n",
				fsobj_path(&s_obj), fsobj_path(&d_obj), s_rc);
		    rc = -1;
		    break;
		}
		
		fsobj_reset(&d_obj);
		fsobj_reset(&s_obj);
	    }
	}

        fsobj_fini(&d_obj);
        fsobj_fini(&s_obj);
        if (rc < 0) {
            if (f_ignore)
                goto End;
            return rc;
        }
        break;

    case S_IFREG:
        if (f_debug)
            fprintf(stderr, "*** clone: Doing file\n");
        rc = file_clone(src, dst);
        if (rc < 0) {
            if (f_ignore)
                goto End;
            return -1;
        }
        if (rc > 0) 
            mdiff |= MD_DATA;
        break;

    case S_IFLNK:
        if (f_debug)
            fprintf(stderr, "*** clone: Doing symlink\n");
        rc = symlink_clone(src, dst);
        if (rc < 0) {
            if (f_ignore)
                goto End;
            return -1;
        }
        if (rc > 0)
            mdiff |= MD_DATA;
    }

    if (f_owners) {
        rc = owner_clone(src, dst);
        if (rc < 0 && !f_ignore)
            return -1;
        if (rc > 0)
            mdiff |= MD_UID;
    }

    if (f_groups) {
        rc = group_clone(src, dst);
        if (rc < 0 && !f_ignore)
            return -1;
        if (rc > 0)
            mdiff |= MD_GID;
    }

    if (f_xattrs) {
        rc = attrs_clone(src, dst);
        if (rc < 0 && !f_ignore)
            return -1;
        if (rc > 0)
            mdiff |= MD_ATTR;
    }

    if (f_modes) {
        rc = modes_clone(src, dst);
	if (rc < 0 && !f_ignore)
	    return -1;
	if (rc > 0)
	    mdiff |= MD_MODE;
    }

    if (f_acls) {
        rc = acls_clone(src, dst);
        if (rc < 0 && !f_ignore)
            return -1;
        if (rc > 0)
            mdiff |= MD_ACL;
    }

    if (f_flags) {
        rc = flags_clone(src, dst);
        if (rc < 0 && !f_ignore)
            return -1;
        if (rc > 0)
            mdiff |= MD_FLAG;
    }

    if (f_timestamps) {
        rc = times_clone(src, dst);
        if (rc < 0 && !f_ignore)
            return -1;
        if (rc > 0)
            mdiff |= MD_TIME;
    }

 End:
    if (f_verbose > 2 || (f_verbose && mdiff)) {
        if (mdiff & MD_NEW) {
        } else {
            if (rc < 0)
                putchar('?');
            else {
                if (mdiff & (MD_DATA|MD_TIME|MD_ATTR|MD_ACL|MD_MODE|MD_UID|MD_GID)) {
                    putchar('!');
                    ++n_updated;
                } else
                    putchar(' ');
            }
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


static void
path2dirbase(char *path,
	     char **dirname,
	     char **basename) {
    char *cp;
    
    cp = strrchr(path, '/');
    if (cp) {
      *cp++ = '\0';
      
      if (!*path)
	*dirname = "/";
      else
	*dirname = path;
      
      if (*cp)
	*basename = cp;
      else
	*basename = ".";
    } else {
      *dirname = ".";
      *basename = path;
    }
}


void
usage(void) {
    int k;

    
    printf("Usage:\n  %s [options] <src> [.. <src-N>] [<dst>]\n\nOptions:\n", argv0);
    for (k = 0; options[k].c != -1; k++)
        printf("  -%c    --%-10s    %s\n",
               options[k].c,
               options[k].l,
               options[k].help);
    
    puts("\nAll options may optionally take a argument. Short options only numeric (-v3)");
    puts("Long options numeric or string (--depth=1k, --checksum=sha256).");
    puts("Numbers may be specified as decimal, octal (preceed with 0) or hexadecimal (preceed 0x)");
    puts("and with an optional suffix (k, m, g, t) multiplier.");

    printf("\nAvailable checksum digests:\n");
    digests_print(stdout, NULL);
    
    exit(0);
}


int
main(int argc,
     char *argv[]) {
    int i, k, rc = 0, c_rc, na;
    char *s, *dirname, *name;
    FSOBJ src_parent, dst_parent;
    
    argv0 = argv[0];

    s = getenv("DEBUG");
    if (s)
      (void) sscanf(s, "%d", &f_debug);
    
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        char *op, *cp = NULL;

        if (argv[i][1] == '-') {
            /* --long[=val] option */
            char *os, *vs;
            int *vp, f_no = 0;
            

            os = argv[i]+2;
            vs = strchr(os, '=');
            if (vs)
                *vs++ = '\0';

            if (!vs && strncmp(os, "no-", 3) == 0) {
                f_no = 1;
                os += 3;
            }

            for (k = 0; options[k].l && strcmp(options[k].l, os) != 0; ++k)
                ;
            if (!options[k].l) {
                if (vs)
                    *--vs = '=';
		fprintf(stderr, "%s: Error: %s: Invalid switch\n",
			argv[0], argv[i]);
		exit(1);
            }

            vp = options[k].vp;
	    if (!vp)
                usage();
            
            if (!vs) {
                if (f_no)
                    *vp = 0;
                else
                    (*vp)++;
            } else {
                long v;
                char *cp;

                v = (*options[k].f)(vs, &cp);
                if (cp == vs) {
                    *--vs = '=';
                    fprintf(stderr, "%s: Error: %s: Missing or invalid switch value\n",
                            argv[0], argv[i]);
                    exit(1);
                }

                *vp = v;
		if (*cp) {
		  switch (tolower(*cp)) {
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
		    fprintf(stderr, "%s: Error: %s: Invalid number suffix\n",
			    argv0, cp);
		    exit(1);
		  }
		}
            }
            continue;
        }
        
	for (op = argv[i]+1; op && *op; op = cp) {
	    int *vp;
	    long v;

	    
	    for (k = 0; options[k].c != -1 && options[k].c != *op; k++)
		;
	    if (options[k].c == -1) {
		fprintf(stderr, "%s: Error: -%c: Invalid option\n",
			argv[0], *op);
		exit(1);
	    }
            
	    if (!options[k].vp)
                usage();

            vp = (int *) options[k].vp;
#if 0
            v = (*options[k].f)(op+1, &cp);
#else
	    v = get_int(op+1, &cp);
#endif
                
            if (cp == op+1) {
                (*vp)++;
            }
            else {
                *vp = v;
                
                if (*cp) {
                    switch (tolower(*cp)) {
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
                        fprintf(stderr, "%s: Error: %s: Invalid number suffix\n",
                                argv0, cp);
                        exit(1);
                    }
                    ++cp;
		}
		op = cp;
	    }
	}
    }

    if (f_verbose)
	printf("[%s - Copyright (c) Peter Eriksson <pen@lysator.liu.se>]\n",
	       PACKAGE_STRING);

    if (f_help)
        usage();
    
    if (f_all) {
	f_owners     += f_all;
	f_groups     += f_all;
	f_recurse    += f_all;
        f_sizes      += f_all;
	f_modes      += f_all;
	f_times      += f_all;
	f_acls       += f_all;
	f_xattrs     += f_all;
	f_flags      += f_all;
        f_timestamps += f_all;
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

    fsobj_init(&src_parent);
    fsobj_init(&src_base);
    
    fsobj_init(&dst_parent);
    fsobj_init(&dst_base);

    if (i >= argc) {
	fprintf(stderr, "%s: Error: Missing required <source> argument\n",
		argv[0]);
        rc = 1;
        goto Fail;
    }

    clock_gettime(CLOCK_REALTIME, &t0);

    na = argc-i;
    if (na < 1) {
	fprintf(stderr, "%s: Error: Missing required arguments (use with --help for usage)\n",
		argv[0]);
        rc = 1;
        goto Fail;
    }
    
    /* Open first source */
    if (f_debug)
	fprintf(stderr, "** Opening first source: %s\n", argv[i]);

    path2dirbase(argv[i], &dirname, &name);

    if (fsobj_open(&src_parent, NULL, dirname, O_PATH, 0) <= 0) {
	fprintf(stderr, "%s: Error: %s: Open(source parent directory): %s\n",
		argv[0], dirname, strerror(errno));
	rc = 1;
	goto Fail;
    }
    
    if (fsobj_open(&src_base, &src_parent, name, O_PATH, 0) <= 0) {
	fprintf(stderr, "%s: Error: %s/%s: Open(source object): %s\n",
		argv[0], dirname, name, strerror(errno));
	rc = 1;
	goto Fail;

    }
    
    if (!f_list) {
        mode_t mode;
      
	/* Open copy target */
	path2dirbase(argv[--argc], &dirname, &name);

	if (fsobj_open(&dst_parent, NULL, dirname, O_PATH, 0) <= 0) {
	    fprintf(stderr, "%s: Error: %s: Open(target parent directory): %s\n",
		    argv[0], dirname, strerror(errno));
	    rc = 1;
	    goto Fail;
	}

#if 1
	if (na < 2)
	    mode = 0777|S_IFDIR;
	else
	    mode = (S_ISDIR(src_base.stat.st_mode) ? 0777 : 0666) | (src_base.stat.st_mode&S_IFMT);
#else
	mode = (na == 2 ? src_base.stat.st_mode : src_parent.stat.st_mode);
#endif
	if (fsobj_open(&dst_base, &dst_parent, name, O_PATH|O_CREAT, mode) < 0) {
	    fprintf(stderr, "%s: Error: %s: Open(target object): %s\n",
		    argv[0], name, strerror(errno));
	    rc = 1;
	    goto Fail;
	}
    }

    
    while (i < argc) {
	int n;

	if (f_list) {
	    printf("Directory Listing of %s:\n", fsobj_path(&src_base));
	    n = dir_list(&src_base, 1);
	    printf("%d total objects\n\n", n);
	} else {
	    c_rc = clone(&src_base, &dst_base, 1);
	    if (c_rc < 0) {
		rc = 1;
		goto End;
	    }
	}

	if (++i < argc) {
	    path2dirbase(argv[i], &dirname, &name);

	    fsobj_reset(&src_base);
	    fsobj_reset(&src_parent);
	    
	    if (fsobj_open(&src_parent, NULL, dirname, O_PATH, 0) <= 0) {
		fprintf(stderr, "%s: Error: %s: Open(source directory): %s\n",
			argv[0], dirname, strerror(errno));
		rc = 1;
		goto Fail;
	    }
	    
	    if (fsobj_open(&src_base, &src_parent, name, O_PATH, 0) <= 0) {
		fprintf(stderr, "%s: Error: %s/%s: Open(source object): %s\n",
			argv[0], dirname, name, strerror(errno));
		rc = 1;
		goto Fail;
		
	    }
	}
    }

 End:
    if (f_verbose || f_stats) {
	double wb, wps, dt0, dt1, dt, dts;
	char *wbu, *wu, *tu;


	clock_gettime(CLOCK_REALTIME, &t1);

	dt0 = t0.tv_sec+t0.tv_nsec/1000000000.0;
	dt1 = t1.tv_sec+t1.tv_nsec/1000000000.0;
	dt = dts = dt1-dt0;

        tu = "s";
	if (dt < 1.0) {
	    dt *= 1000.0;
	    tu = "ms";
	    if (dt < 1.0) {
		dt *= 1000.0;
		tu = "μs";
	    }
	} else {
	    if (dt > 90.0) {
		dt /= 60;
		tu = "m";
	    }
	    if (dt > 90.0) {
		dt /= 60;
		tu = "h";
	    }
	}

	wb = b_written;
	wbu = "B";
	if (wb  > 1000) {
	    wb /= 1000;
	    wbu = "kB";
	}
	if (wb  > 1000) {
	    wb /= 1000;
	    wbu = "MB";
	}
	if (wb  > 1000) {
	    wb /= 1000;
	    wbu = "GB";
	}
        
	wps = b_written / dts;
        wu = "B/s";
	if (wps > 1000) {
	    wps /= 1000;
	    wu = "kB/s";
	}
	if (wps > 1000) {
	    wps /= 1000;
	    wu = "MB/s";
	}
	if (wps > 1000) {
	    wps /= 1000;
	    wu = "GB/s";
	}

	printf("[%lu scanned in %.1f %s (%.0f/%s); %lu added (%.0f/s), %lu updated, %lu deleted; %.1f %s written (%.0f %s)]\n",
	       n_scanned, dt, tu, n_scanned/dt, tu,
               n_added, n_added/dt, n_updated, n_deleted,
	       wb, wbu, wps, wu);
    }

 Fail:
    fsobj_fini(&src_base);
    fsobj_fini(&src_parent);
    
    fsobj_fini(&dst_base);
    fsobj_fini(&dst_parent);

    exit(rc);
}
