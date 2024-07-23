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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <sys/extattr.h>
#include <dirent.h>

int f_debug = 0;
int f_verbose = 0;
int f_noxdev = 0;
int f_update = 1;
int f_recurse = 0;
int f_times = 0;
int f_owner = 0;
int f_group = 0;
int f_acl = 0;
int f_xattr = 0;
int f_prune = 0;
int f_force = 0;

char *argv0 = "pxcp";

dev_t s_dev = 0;
dev_t d_dev = 0;

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


static int acl_perms_posix[] = {
    ACL_EXECUTE,
    ACL_WRITE,
    ACL_READ
};

static int acl_perms_nfs4[] = {
    ACL_READ_DATA,
    ACL_LIST_DIRECTORY,
    ACL_WRITE_DATA,
    ACL_ADD_FILE,
    ACL_APPEND_DATA,
    ACL_ADD_SUBDIRECTORY,
    ACL_READ_NAMED_ATTRS,
    ACL_WRITE_NAMED_ATTRS,
    ACL_EXECUTE,
    ACL_DELETE_CHILD,
    ACL_READ_ATTRIBUTES,
    ACL_WRITE_ATTRIBUTES,
    ACL_DELETE,
    ACL_READ_ACL,
    ACL_WRITE_ACL,
    ACL_SYNCHRONIZE
};

static int acl_flags_nfs4[] = {
    ACL_ENTRY_FILE_INHERIT,
    ACL_ENTRY_DIRECTORY_INHERIT,
    ACL_ENTRY_NO_PROPAGATE_INHERIT,
    ACL_ENTRY_INHERIT_ONLY,
    ACL_ENTRY_INHERITED
};


int
acl_diff(acl_t src,
	 acl_t dst) {
    int j, d_rc, s_rc, s_b, d_b;
    acl_entry_t s_e, d_e;


    /* Check brand */
    if (acl_get_brand_np(src, &s_b) < 0)
	return -1;
    if (acl_get_brand_np(dst, &d_b) < 0)
	return -1;
    if (s_b != d_b)
	return 1;
    
    s_rc = acl_get_entry(src, ACL_FIRST_ENTRY, &s_e);
    d_rc = acl_get_entry(dst, ACL_FIRST_ENTRY, &d_e);
    while (s_rc > 0 && d_rc > 0) {
	acl_permset_t s_ps, d_ps;
	acl_tag_t s_t, d_t;
	void *s_q, *d_q;
	uid_t s_u, d_u;
	gid_t s_g, d_g;

	
	/* OWNER@, GROUP@, user:, group:, EVERYONE@ etc */
	if (acl_get_tag_type(s_e, &s_t) < 0)
	    return -1;
	if (acl_get_tag_type(d_e, &d_t) < 0)
	    return -1;
	
	if (s_t != d_t)
	    return 2;
	
	switch (s_t) {
	case ACL_USER:
	    s_q = acl_get_qualifier(s_e);
	    if (!s_q)
		return -1;
	    d_q = acl_get_qualifier(s_e);
	    if (!d_q) {
		acl_free(s_q);
		return -1;
	    }
	    s_u = * (uid_t *) s_q;
	    d_u = * (uid_t *) d_q;
	    acl_free(s_q);
	    acl_free(d_q);
	    if (s_u != d_u)
		return 3;
	    break;
	case ACL_GROUP:
	    s_q = acl_get_qualifier(s_e);
	    if (!s_q)
		return -1;
	    d_q = acl_get_qualifier(s_e);
	    if (!d_q) {
		acl_free(s_q);
		return -1;
	    }
	    s_g = * (gid_t *) s_q;
	    d_g = * (gid_t *) d_q;
	    acl_free(s_q);
	    acl_free(d_q);
	    if (s_g != d_g)
		return 4;
	    break;
	}

	/* Check permset */
	if (acl_get_permset(s_e, &s_ps) < 0)
	    return -1;
	if (acl_get_permset(d_e, &d_ps) < 0)
	    return -1;

	switch (s_b) {
	case ACL_BRAND_POSIX:
	    for (j = 0; j < sizeof(acl_perms_posix)/sizeof(acl_perms_posix[0]); j++) {
		acl_perm_t s_p, d_p;
		
		s_p = acl_get_perm_np(s_e, acl_perms_posix[j]);
		d_p = acl_get_perm_np(d_e, acl_perms_posix[j]);
		if (s_p != d_p)
		    return 5;
	    }
	    break;
	case ACL_BRAND_NFS4:
	    for (j = 0; j < sizeof(acl_perms_nfs4)/sizeof(acl_perms_nfs4[0]); j++) {
		acl_perm_t s_p, d_p;
		
		s_p = acl_get_perm_np(s_e, acl_perms_nfs4[j]);
		d_p = acl_get_perm_np(d_e, acl_perms_nfs4[j]);
		if (s_p != d_p)
		    return 6;
	    }
	    break;
	default:
	    return -1;
	}

	if (s_b == ACL_BRAND_NFS4) {
	    acl_flagset_t s_fs, d_fs;
	    acl_entry_type_t s_et, d_et;
	    
	    /* Check flagset */
	    if (acl_get_flagset_np(s_e, &s_fs) < 0)
		return -1;
	    if (acl_get_flagset_np(d_e, &d_fs) < 0)
		return -1;
	    
	    for (j = 0; j < sizeof(acl_flags_nfs4)/sizeof(acl_flags_nfs4[0]); j++) {
		acl_flag_t s_f, d_f;
		
		s_f = acl_get_flag_np(s_e, acl_flags_nfs4[j]);
		d_f = acl_get_flag_np(d_e, acl_flags_nfs4[j]);
		if (s_f != d_f)
		    return 7;
	    }
	    
	    /* ALLOW/DENY */
	    if (acl_get_entry_type_np(s_e, &s_et) < 0)
		return -1;
	    if (acl_get_entry_type_np(d_e, &d_et) < 0)
		return 1;
	    if (s_et != d_et)
		return 8;
	}
	
	s_rc = acl_get_entry(src, ACL_NEXT_ENTRY, &s_e);
	d_rc = acl_get_entry(dst, ACL_NEXT_ENTRY, &d_e);
    }

    if (s_rc != d_rc)
	return 9;
    
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
do_copy(int s_fd,
	const char *s_dirpath,
	int d_dirfd,
	const char *d_dirpath,
	const char *d_name,
	mode_t mode,
	int *d_fdp) {
    char *tmpname = ".pxcp_tmpfile";
    int d_fd;
    char buf[128*1024];
    ssize_t got, rc = 0;
    

    if (f_debug)
	fprintf(stderr, "*** do_copy(%d, %s, %d, %s, %s)\n", s_fd, s_dirpath, d_dirfd, d_dirpath, d_name);
    
    d_fd = openat(d_dirfd, tmpname, O_CREAT|O_WRONLY, mode);
    if (d_fd < 0) {
	fprintf(stderr, "%s: Error: %s/%s: openat(O_CREAT): %s\n",
		argv0, d_dirpath, tmpname, strerror(errno));
	return -1;
    }

    while ((got = read(s_fd, buf, sizeof(buf))) > 0) {
        rc = write(d_fd, buf, got);
	if (rc < 0) {
	    fprintf(stderr, "%s: Error: %s/%s: write: %s\n",
		    argv0, d_dirpath, tmpname, strerror(errno));
	    rc = -2;
	    goto End;
	}
	if (rc != got) {
	    fprintf(stderr, "%s: Error: %s/%s: Short write (%ld bytes)\n",
		    argv0, d_dirpath, tmpname, got);
	    rc = -3;
	    goto End;
	}
    }
    if (got < 0) {
	fprintf(stderr, "%s: Error: %s/%s: read: %s\n",
		argv0, s_dirpath, d_name, strerror(errno));
	rc = -4;
	goto End;
    }

    if (renameat(d_dirfd, tmpname, d_dirfd, d_name) < 0) {
	fprintf(stderr, "%s: Error: %s/%s -> %s/%s: renameat: %s\n",
		argv0, d_dirpath, tmpname, d_dirpath, d_name, strerror(errno));
	rc = -4;
    }

 End:
    if (rc)
	(void) unlinkat(d_dirfd, tmpname, AT_RESOLVE_BENEATH);

    if (d_fdp)
	*d_fdp = d_fd;
    else
	close(d_fd);
    
    return 0;
}



int
do_update(int s_dirfd,
	  const char *s_dirpath,
	  int d_dirfd,
	  const char *d_dirpath) {
    ssize_t buflen;
    char dirbuf[65536], *bufp;
    int rc = 0;
    int s_fd = -1, d_fd = -1;
    acl_t s_acl = NULL, d_acl = NULL;
    

    if (f_debug)
	fprintf(stderr, "*** do_update(%d, %s, %d, %s)\n",
		s_dirfd, s_dirpath, d_dirfd, d_dirpath);
    
    while (rc == 0 && (buflen = getdents(s_dirfd, dirbuf, sizeof(dirbuf))) > 0) {
	bufp = dirbuf;
	
	while (rc == 0 && bufp < dirbuf+buflen) {
	    struct dirent *dep = (struct dirent *) bufp;
	    unsigned int mdiff = 0;
	    struct stat s_sb, d_sb;
	    
	    
	    if (dep->d_name[0] == '.' && (dep->d_name[1] == '\0' || (dep->d_name[1] == '.' && dep->d_name[2] == '\0')))
		goto Next;
		
	    s_fd = openat(s_dirfd, dep->d_name, O_RDONLY|O_NOFOLLOW);
	    if (s_fd < 0) {
		fprintf(stderr, "%s: Error: %s/%s: openat: %s\n",
			argv0, s_dirpath, dep->d_name, strerror(errno));
		rc = -1;
		goto End;
	    }
	    
	    if (fstatat(s_fd, "", &s_sb, AT_EMPTY_PATH) < 0) {
		fprintf(stderr, "%s: Error: %s/%s: fstatat: %s\n",
			argv0, s_dirpath, dep->d_name, strerror(errno));
		rc = -2;
		goto End;
	    }

	    if (d_dirfd < 0) {
		/* Destination directory not opened (does not exist & dryrun mode) */
		mdiff |= MD_NEW;
		d_fd = -1;
	    } else {
		d_fd = openat(d_dirfd, dep->d_name, O_RDONLY|O_NOFOLLOW);
		if (d_fd < 0) {
		    if (f_update) {
			if (S_ISDIR(s_sb.st_mode)) {
			    if (mkdirat(d_dirfd, dep->d_name, s_sb.st_mode&ALLPERMS) < 0) {
				fprintf(stderr, "%s: Error: %s/%s: mkdirat: %s\n",
					argv0, d_dirpath, dep->d_name, strerror(errno));
				rc = -3;
				goto End;
			    }
			} else {
			    if (do_copy(s_fd, s_dirpath, d_dirfd, d_dirpath, dep->d_name, s_sb.st_mode&ALLPERMS, &d_fd) < 0) {
				rc = -4;
				goto End;
			    }
			}
		    }
		    mdiff |= MD_NEW;
		}
	    }

	    if (d_fd >= 0) {
		if (fstatat(d_fd, "", &d_sb, AT_EMPTY_PATH) < 0) {
		    fprintf(stderr, "%s: Error: %s/%s: openat: %s\n",
			    argv0, d_dirpath, dep->d_name, strerror(errno));
		    
		    rc = -4;
		    goto End;
		}
		
		if (S_ISREG(s_sb.st_mode) &&
		    (f_force ||
		     s_sb.st_size != d_sb.st_size ||
		     ts_isless(&d_sb.st_mtim, &s_sb.st_mtim))) {
		    if (f_update) {
			int n_fd = -1;
			
			if (do_copy(s_fd, s_dirpath, d_dirfd, d_dirpath, dep->d_name, s_sb.st_mode&ALLPERMS, &n_fd) < 0) {
			    rc = -4;
			    goto End;
			}
			close(d_fd);
			d_fd = n_fd;
		    }
		    mdiff |= MD_DATA;
		}
		
		if (f_owner && (f_force || s_sb.st_uid != d_sb.st_uid)) {
		    if (f_update) {
			if (fchownat(d_dirfd, dep->d_name, s_sb.st_uid, -1, AT_SYMLINK_NOFOLLOW|AT_RESOLVE_BENEATH) < 0) {
			    fprintf(stderr, "%s: Error: %s/%s: fchownat: %s\n",
				    argv0, d_dirpath, dep->d_name, strerror(errno));
			    rc = -5;
			    goto End;
			}
		    }
		    mdiff |= MD_UID;
		}
		
		if (f_group && (f_force || s_sb.st_gid != d_sb.st_gid)) {
		    if (f_update) {
			if (fchownat(d_dirfd, dep->d_name, -1, s_sb.st_gid, AT_SYMLINK_NOFOLLOW|AT_RESOLVE_BENEATH) < 0) {
			    fprintf(stderr, "%s: Error: %s/%s: fchownat: %s\n",
				    argv0, d_dirpath, dep->d_name, strerror(errno));
			    rc = -5;
			    goto End;
			}
		    }
		    mdiff |= MD_GID;
		}
		
		if (f_force || (s_sb.st_mode&ALLPERMS) != (d_sb.st_mode&ALLPERMS)) {
		    if (f_update) {
			if (fchmodat(d_dirfd, dep->d_name, s_sb.st_mode&ALLPERMS, AT_SYMLINK_NOFOLLOW|AT_RESOLVE_BENEATH) < 0) {
			    fprintf(stderr, "%s: Error: %s/%s: fchmodat: %s\n",
				    argv0, d_dirpath, dep->d_name, strerror(errno));
			    rc = -6;
			    goto End;
			}
		    }
		    mdiff |= MD_MODE;
		}
		

		if (f_xattr) {
		    ssize_t s_alen, d_alen;
		    int s_i, d_i;
		    
		    s_alen = extattr_list_fd(s_fd, EXTATTR_NAMESPACE_USER, NULL, 0);
		    d_alen = extattr_list_fd(d_fd, EXTATTR_NAMESPACE_USER, NULL, 0);
		    
		    if (s_alen > 0 || d_alen > 0) {
			char *s_alist = NULL, *d_alist = NULL;

			if (s_alen > 0) {
			    s_alist = (char *) malloc(s_alen);
			    if (!s_alist) {
				fprintf(stderr, "%s: Error: %ld: malloc: %s\n",
					argv0, s_alen, strerror(errno));
				rc = -7;
				goto End;
			    }
			    s_alen = extattr_list_fd(s_fd, EXTATTR_NAMESPACE_USER, s_alist, s_alen);
			}
			
			if (d_alen > 0) {
			    d_alist = (char *) malloc(d_alen);
			    if (!d_alist) {
				fprintf(stderr, "%s: Error: %ld: malloc: %s\n",
					argv0, d_alen, strerror(errno));
				free(s_alist);
				s_alist = NULL;
				rc = -8;
				goto End;
			    }
			    d_alen = extattr_list_fd(d_fd, EXTATTR_NAMESPACE_USER, d_alist, d_alen);
			}

			s_i = 0;
			while (s_i < s_alen) {
			    char *aname;
			    ssize_t s_adlen, d_adlen;
			    void *s_adata = NULL, *d_adata = NULL;
			    
			    unsigned char s_anlen = s_alist[s_i++];
			    if (s_anlen == 0)
				continue;

			    /* Scan for matching ExtAttr */
			    d_i = 0;
			    while (d_i < d_alen) {
				unsigned char d_anlen = d_alist[d_i++];

				if (d_anlen == 0)
				    continue;

				if (s_anlen == d_anlen && memcmp(s_alist+s_i, d_alist+d_i, s_anlen) == 0)
				    break;
			    }

			    aname = strndup(s_alist+s_i, s_anlen);
			    
			    s_adlen = extattr_get_fd(s_fd, EXTATTR_NAMESPACE_USER, aname, NULL, 0);
			    s_adata = malloc(s_adlen);
			    s_adlen = extattr_get_fd(s_fd, EXTATTR_NAMESPACE_USER, aname, s_adata, s_adlen);
			    
			    if (d_i < d_alen) {
				d_adlen = extattr_get_fd(d_fd, EXTATTR_NAMESPACE_USER, aname, NULL, 0);

				if (s_adlen == d_adlen) {
				    d_adata = malloc(d_adlen);
				    d_adlen = extattr_get_fd(d_fd, EXTATTR_NAMESPACE_USER, aname, d_adata, d_adlen);

				    if (memcmp(d_adata, s_adata, s_adlen) != 0) {
					if (f_update) {
					    extattr_set_fd(d_fd, EXTATTR_NAMESPACE_USER, aname, s_adata, s_adlen);
					}	
					if (f_verbose > 1)
					    printf("  ! %s/%s : %.*s\n", d_dirpath, dep->d_name, s_anlen, s_alist+s_i);
					mdiff |= MD_ATTR;
				    }
				    free(d_adata);
				    d_adata = NULL;
				} else {
				    if (f_update) {
					extattr_set_fd(d_fd, EXTATTR_NAMESPACE_USER, aname, s_adata, s_adlen);
				    }
				    if (f_verbose > 1)
					printf("  ! %s/%s : %.*s\n", d_dirpath, dep->d_name, s_anlen, s_alist+s_i);
				    mdiff |= MD_ATTR;
				}
			    } else {
				if (f_update) {
				    extattr_set_fd(d_fd, EXTATTR_NAMESPACE_USER, aname, s_adata, s_adlen);
				}
				if (f_verbose > 1)
				    printf("  + %s/%s : %.*s\n", d_dirpath, dep->d_name, s_anlen, s_alist+s_i);
				mdiff |= MD_ATTR;
			    }
			    free(s_adata);
			    s_adata = NULL;
			    free(aname);
			    aname = NULL;

			    s_i += s_anlen;
			}

			/* Scan for ExtAttrs to remove */
			d_i = 0;
			while (d_i < d_alen) {
			    unsigned char d_anlen = d_alist[d_i++];
			    if (d_anlen == 0)
				continue;

			    /* Scan for matching ExtAttr */
			    s_i = 0;
			    while (s_i < s_alen) {
				unsigned char s_anlen = s_alist[s_i++];
				if (s_anlen == 0)
				    continue;

				if (d_anlen == s_anlen && memcmp(s_alist+s_i, d_alist+d_i, s_anlen) == 0)
				    break;
			    }
			    if (s_i >= s_alen) {
				if (f_update) {
				    char *aname = strndup((char *) d_alist+d_i, d_anlen);
				    
				    if (extattr_delete_fd(d_fd, EXTATTR_NAMESPACE_USER, aname) < 0) {
					fprintf(stderr, "%s: Error: %s/%s: %s: extattr_delete: %s\n",
						argv0, d_dirpath, dep->d_name, aname, strerror(errno));
					free(aname);
					aname = NULL;
					rc = -8;
					goto End;
				    }
				    free(aname);
				    aname = NULL;
				}
				if (f_verbose > 1)
				    printf("  - %s/%s : %.*s\n", d_dirpath, dep->d_name, d_anlen, d_alist+d_i);
				mdiff |= MD_ATTR;
			    }

			    d_i += d_anlen;
			}
			
			if (s_alist)
			    free(s_alist);
			if (d_alist)
			    free(d_alist);
		    }
		}
		
		if (f_acl) {
		    acl_type_t s_t, d_t;
		    
		    s_acl = acl_get_fd_np(s_fd, s_t = ACL_TYPE_NFS4);
		    if (!s_acl)
			s_acl = acl_get_fd_np(s_fd, s_t = ACL_TYPE_ACCESS);
		    
		    d_acl = acl_get_fd_np(d_fd, d_t = ACL_TYPE_NFS4);
		    if (!d_acl)
			d_acl = acl_get_fd_np(d_fd, d_t = ACL_TYPE_ACCESS);
		    
		    if (s_acl && (f_force || !d_acl || (rc = acl_diff(s_acl, d_acl)))) {
			if (f_update) {
			    if (acl_set_fd_np(d_fd, s_acl, s_t) < 0) {
				fprintf(stderr, "%s: Error: %s/%s: acl_set_fd_np: %s\n",
					argv0, d_dirpath, dep->d_name, strerror(errno));
				rc = -5;
				goto End;
			    }
			}
			mdiff = MD_ACL;
		    }
		}
		
		if (f_times) {
		    if (f_force ||
			s_sb.st_birthtim.tv_sec != d_sb.st_birthtim.tv_sec || 
			s_sb.st_birthtim.tv_nsec != d_sb.st_birthtim.tv_nsec) {
			if (f_update) {
			    struct timespec times[2];
			    
			    times[0] = s_sb.st_atim;
			    times[1] = s_sb.st_birthtim;
			    if (utimensat(d_dirfd, dep->d_name, times, AT_RESOLVE_BENEATH) < 0) {
				fprintf(stderr, "%s: Error: %s/%s: utimes(btime): %s\n",
					argv0, d_dirpath, dep->d_name, strerror(errno));
				rc = -6;
				goto End;
			    }
			}
			mdiff |= MD_BTIME;
		    }
		    if (f_force ||
			s_sb.st_mtim.tv_sec != d_sb.st_mtim.tv_sec || 
			s_sb.st_mtim.tv_nsec != d_sb.st_mtim.tv_nsec) {
			if (f_update) {
			    struct timespec times[2];
			    
			    times[0] = s_sb.st_atim;
			    times[1] = s_sb.st_mtim;
			    if (utimensat(d_dirfd, dep->d_name, times, AT_RESOLVE_BENEATH) < 0) {
				fprintf(stderr, "%s: Error: %s/%s: utimes(mtime): %s\n",
					argv0, d_dirpath, dep->d_name, strerror(errno));
				rc = -7;
				goto End;
			    }
			    
			}
			mdiff |= MD_MTIME;
		    }
		}
	    }
	    
	    if (f_verbose > 1 || (f_verbose && mdiff)) {
		if (mdiff & MD_NEW)
		    putchar('+');
		else if (mdiff & (MD_DATA|MD_MTIME|MD_BTIME|MD_ATTR|MD_ACL|MD_MODE|MD_UID|MD_GID))
		    putchar('!');
		else
		    putchar(' ');
		printf(" %s/%s -> %s/%s", s_dirpath, dep->d_name, d_dirpath, dep->d_name);
		if (f_debug)
		    printf(" [%04x]", mdiff);
		putchar('\n');
	    }

	    if (f_recurse && S_ISDIR(s_sb.st_mode) && (!f_noxdev || s_sb.st_dev == s_dev)) {
		int rc;
		char *s_dirbuf = NULL, *d_dirbuf = NULL;

		if (asprintf(&s_dirbuf, "%s/%s", s_dirpath, dep->d_name) < 0) {
		    fprintf(stderr, "%s: Error: asprintf: %s\n",
			    argv0, strerror(errno));
		    rc = -7;
		    goto End;
		}
		if (asprintf(&d_dirbuf, "%s/%s", d_dirpath, dep->d_name) < 0) {
		    fprintf(stderr, "%s: Error: asprintf: %s\n",
			    argv0, strerror(errno));
		    rc = -8;
		    free(s_dirbuf);
		    s_dirbuf = NULL;
		    goto End;
		}
		
		rc = do_update(s_fd, s_dirbuf, d_fd, d_dirbuf);

		/* XXX: Reset utimes */
		
		if (s_dirbuf) {
		    free(s_dirbuf);
		    s_dirbuf = NULL;
		}
		if (d_dirbuf) {
		    free(d_dirbuf);
		    d_dirbuf = NULL;
		}
		
		if (rc)
		    goto End;
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

	    if (s_fd >= 0) {
		close(s_fd);
		s_fd = -1;
	    }
	    
	    if (d_fd >= 0) {
		close(d_fd);
		d_fd = -1;
	    }

	    bufp += dep->d_reclen;
	}
    }

 End:
    if (s_acl)
	acl_free(s_acl);
    
    if (d_acl)
	acl_free(d_acl);

    if (s_fd >= 0)
	close(s_fd);
    
    if (d_fd >= 0)
	close(d_fd);
    
    return rc;
}


int
do_prune(int s_dirfd,
	 const char *s_dirpath,
	 int d_dirfd,
	 const char *d_dirpath) {
    ssize_t buflen;
    char dirbuf[65536], *bufp;
    int rc = 0;
    int s_fd = -1, d_fd = -1;
    

    if (f_debug)
	fprintf(stderr, "*** do_prune(%d, %s, %d, %s)\n",
		s_dirfd, s_dirpath,
		d_dirfd, d_dirpath);
    
    while ((buflen = getdents(d_dirfd, dirbuf, sizeof(dirbuf))) > 0) {
	bufp = dirbuf;
	    
	while (bufp < dirbuf+buflen) {
	    struct dirent *dep = (struct dirent *) bufp;
	    struct stat d_sb;
	    
	    if (dep->d_name[0] == '.' && (dep->d_name[1] == '\0' || (dep->d_name[1] == '.' && dep->d_name[2] == '\0')))
		goto Next;


	    d_fd = openat(d_dirfd, dep->d_name, O_RDONLY|O_NOFOLLOW);
	    if (fstatat(d_fd, "", &d_sb, AT_EMPTY_PATH) < 0) {
		fprintf(stderr, "%s: Error: %s/%s: fstatat: %s\n",
			argv0, d_dirpath, dep->d_name, strerror(errno));
		rc = -1;
		goto End;
	    }


	    if (s_dirfd >= 0)
		s_fd = openat(s_dirfd, dep->d_name, O_PATH|O_NOFOLLOW);
	    
	    if (f_recurse && S_ISDIR(d_sb.st_mode) && (!f_noxdev || d_sb.st_dev == d_dev)) {
		int rc;
		char *s_dirbuf = NULL, *d_dirbuf = NULL;

		if (asprintf(&s_dirbuf, "%s/%s", s_dirpath, dep->d_name) < 0) {
		    fprintf(stderr, "%s: Error: asprintf: %s\n",
			    argv0, strerror(errno));
		    rc = -2;
		    goto End;
		}
		if (asprintf(&d_dirbuf, "%s/%s", d_dirpath, dep->d_name) < 0) {
		    fprintf(stderr, "%s: Error: asprintf: %s\n",
			    argv0, strerror(errno));
		    free(s_dirbuf);
		    s_dirbuf = NULL;
		    rc = -3;
		    goto End;
		}
		
		rc = do_prune(s_fd, s_dirbuf, d_fd, d_dirbuf);

		if (s_dirbuf) {
		    free(s_dirbuf);
		    s_dirbuf = NULL;
		}

		if (d_dirbuf) {
		    free(d_dirbuf);
		    d_dirbuf = NULL;
		}
		
		if (rc)
		    goto End;
	    }

	    
	    if (s_fd < 0) {
		if (f_update) {
		    if (funlinkat(d_dirfd, dep->d_name, d_fd, AT_RESOLVE_BENEATH|(S_ISDIR(d_sb.st_mode) ? AT_REMOVEDIR : 0)) < 0) {
			fprintf(stderr, "%s: Error: %s/%s: funlinkat: %s\n",
				argv0, d_dirpath, dep->d_name, strerror(errno));
			rc = -4;
			goto End;
		    }
		}
		printf("- %s/%s\n", d_dirpath, dep->d_name);
	    }
	    
	Next:
	    bufp += dep->d_reclen;
	}
    }
    
 End:
    if (d_fd >= 0)
	close(d_fd);
	    
    if (s_fd >= 0)
	close(s_fd);

    return rc;
}

int
main(int argc,
     char *argv[]) {
    int i, j, rc, s_fd, d_fd;
    

    argv0 = argv[0];
    
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
	for (j = 1; argv[i][j]; j++) {
	    switch (argv[i][j]) {
	    case 'd':
		f_debug++;
		break;
	    case 'm':
		f_prune++;
		/* Fall-thru */
	    case 'a':
		f_recurse++;
		f_owner++;
		f_group++;
		f_times++;
		f_acl++;
		f_xattr++;
		break;
	    case 'v':
		f_verbose++;
		break;
	    case 'f':
		f_force++;
		break;
	    case 'n':
		f_update = 0;
		break;
	    case 'x':
		f_noxdev++;
		break;
	    case 'r':
		f_recurse++;
		break;
	    case 't':
		f_times++;
		break;
	    case 'u':
		f_owner++;
		break;
	    case 'g':
		f_group++;
		break;
	    case 'p':
		f_prune++;
		break;
	    case 'A':
		f_acl++;
		break;
	    case 'X':
		f_xattr++;
		break;
	    case 'h':
		printf("Usage:\n  %s [-hvfnxrpamAX] <srcdir> <dstdir>\n", argv[0]);
		exit(0);
	    default:
		fprintf(stderr, "%s: Error: -%c: Invalid switch\n",
			argv[0], argv[i][j]);
		exit(1);
	    }
	}
    }

    if (i >= argc) {
	fprintf(stderr, "%s: Error: Missing required <src> argument\n",
		argv[0]);
	exit(1);
    }



    s_fd = open(argv[i], O_RDONLY|O_DIRECTORY);
    if (s_fd < 0) {
	fprintf(stderr, "%s: Error: %s: open: %s\n",
		argv[0], argv[i], strerror(errno));
	exit(1);
    }
    if (f_noxdev) {
	struct stat sb;
	
	if (fstat(s_fd, &sb) < 0) {
	    fprintf(stderr, "%s: Error: %s: fstat: %s\n",
		    argv[0], argv[i], strerror(errno));
	    exit(1);
	}
	s_dev = sb.st_dev;
    }

    d_fd = open(argv[i+1], O_RDONLY|O_DIRECTORY);
    if (d_fd < 0) {
	fprintf(stderr, "%s: Error: %s: open: %s\n",
		argv[0], argv[i+1], strerror(errno));
	exit(1);
    }
    
    if (f_noxdev) {
	struct stat sb;
	
	if (fstat(d_fd, &sb) < 0) {
	    fprintf(stderr, "%s: Error: %s: fstat: %s\n",
		    argv[0], argv[i+1], strerror(errno));
	    exit(1);
	}
	d_dev = sb.st_dev;
    }
    
    rc = do_update(s_fd, argv[i], d_fd, argv[i+1]);
    if (rc)
	exit(1);
    if (f_prune)
	rc = do_prune(s_fd, argv[i], d_fd, argv[i+1]);

    close(d_fd);
    close(s_fd);
    exit(rc ? 1 : 0);
}
