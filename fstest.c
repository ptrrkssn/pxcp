/*
 * fstest.c - Test OS & filesystem capabilities
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
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_ACL_H
#include <sys/acl.h>
#endif

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif


#ifdef HAVE_ACL_LIBACL_H
#include <acl/libacl.h>
#define LINUX_NFS4_ACL_XATTR "system.nfs4_acl"
#endif

struct {
  int f;
  char *s;
} o_flags[] = {
  { O_RDONLY, "RDONLY" },
  { O_RDWR, "RDWR" },
#ifdef O_NOFOLLOW
  { O_RDONLY|O_NOFOLLOW, "RDONLY|NOFOLLOW" },
#endif
#ifdef O_SYMLINK
  { O_RDONLY|O_SYMLINK, "RDONLY|SYMLINK" },
# ifdef O_NOFOLLOW
  { O_RDONLY|O_SYMLINK|O_NOFOLLOW, "RDONLY|SYMLINK|NOFOLLOW" },
# endif
#endif


#ifdef O_PATH
  { O_PATH, "PATH" },
#ifdef O_NOFOLLOW
  { O_PATH|O_NOFOLLOW, "PATH|NOFOLLOW" },
#endif
#endif
  
#ifdef O_SEARCH
  { O_SEARCH, "SEARCH" },
#endif

  { 0, NULL },
}; 

struct {
    int f;
    char *s;
} a_flags[] = {
    { ACL_TYPE_ACCESS, "ACL_TYPE_ACCESS" },
#ifdef ACL_TYPE_NFS4
    { ACL_TYPE_NFS4, "ACL_TYPE_NFS4" },
#endif
#ifdef __APPLE__
    { ACL_TYPE_EXTENDED, "ACL_TYPE_EXTENDED" },
#endif
    { 0, NULL }
};


void
check_acl(acl_t a,
	  int t,
	  char *ts,
	  int fd,
	  char *path) {
    int rc;

    
    if (!a)
	return;
    
#ifdef __linux__
    if (a) {
	mode_t m = 0;
	
	errno = 0;
	rc = acl_equiv_mode(a, &m);
	printf("      acl_equiv_mode(%s) -> %d [%04o] (%d=%s)\n",
	       path, rc, m, errno, strerror(errno));
    }
#endif
#ifdef __FreeBSD__
    if (a) {
	int t = 0;
        
        errno = 0;	
	rc = acl_is_trivial_np(a, &t);
	printf("      acl_is_trivial_np(%s) -> %d [t=%d] (%d=%s)\n",
	       path, (int) rc, t, errno, strerror(errno));
    }
#endif
#ifdef __APPLE__
    if (a) {
	errno = 0;
	rc = acl_valid(a);
	printf("      acl_valid(%s) -> %d (%d=%s)\n",
	       path, rc, errno, strerror(errno));

	if (fd >= 0) {
	    errno = 0;
	    rc = acl_valid_fd_np(fd, t, a);
	    printf("      acl_valid_fd_np(%d=%s, %s) -> %d (%d=%s)\n",
		   fd, path, ts, rc, errno, strerror(errno));
	}
	
	errno = 0;
	rc = acl_valid_file_np(path, t, a);
	printf("      acl_valid_file_np(%s, %s) -> %d (%d=%s)\n",
	       path, ts, rc, errno, strerror(errno));
    }
#endif
}


void
iputs(int indent,
      char *s) {
  int i;
  while (*s) {
    for (i = 0; i < indent; i++)
      putchar(' ');
    while (*s && *s != '\n')
      putchar(*s++);
    putchar('\n');
    if (*s)
      ++s;
  }
}

int
main(int argc,
     char *argv[]) {
    int dfd, fd, nfd, i, j, k;
    ssize_t rc;
    acl_t a;
    struct stat sb;
    void *va;
    char buf[1024];
    

    errno = 0;
    dfd = open(".", O_RDONLY);
    if (dfd < 0) {
	perror("open(.)");
	exit(1);
    }
    
    for (i = 1; i < argc; i++) {
	printf("\n%s:\n", argv[i]);
	
	errno = 0;
	rc = lstat(argv[i], &sb);
	printf("  lstat(%s) -> %d [m=%04o] (%d=%s)\n",
	       argv[i], (int) rc, sb.st_mode&ALLPERMS, errno, strerror(errno));

	if (rc == 0) {
	    rc = lchmod(argv[i], sb.st_mode&ALLPERMS);
	    printf("  lchmod(%s, %04o) -> %d (%d=%s)\n",
		   argv[i], sb.st_mode&ALLPERMS, (int) rc, errno, strerror(errno));
	    
            errno = 0;
	    rc = fchmodat(AT_FDCWD, argv[i], sb.st_mode&ALLPERMS, AT_SYMLINK_NOFOLLOW);
	    printf("  fchmodat(AT_FDCWD, %s, %04o, AT_SYMLINK_NOFOLLOW) -> %d (%d=%s)\n",
		   argv[i], sb.st_mode&ALLPERMS, (int) rc, errno, strerror(errno));

            if (S_ISLNK(sb.st_mode)) {
                errno = 0;
                rc = readlink(argv[i], buf, sizeof(buf));
                printf("  readlink(%s) -> %d (%d=%s)\n",
                       argv[i], (int) rc, errno, strerror(errno));

#if 0
                if (rc >= 0) {
                    buf[rc] = '\0';
                    rc = symlink(buf, argv[i]);
                    printf("  symlink(%s, %s) -> %d (%d=%s)\n",
                           buf, argv[i], (int) rc, errno, strerror(errno));
                }
#endif
                
                errno = 0;
                rc = readlinkat(dfd, argv[i], buf, sizeof(buf));
                printf("  readlinkat(%d, %s) -> %d (%d=%s)\n",
                       dfd, argv[i], (int) rc, errno, strerror(errno));

#if 0
                if (rc >= 0) {
                    buf[rc] = '\0';
                    rc = symlinkat(buf, dfd, argv[i]);
                    printf("  symlinkat(%s, %d, %s) -> %d (%d=%s)\n",
                           buf, dfd, argv[i], (int) rc, errno, strerror(errno));
                }
#endif
            }
        }
	
#ifdef __linux__
	errno = 0;
	rc = lgetxattr(argv[i], LINUX_NFS4_ACL_XATTR, NULL, 0);
	printf("  lgetxattr(%s) -> %lld (%d=%s)\n",
	       argv[i], (long long int) rc, errno, strerror(errno));

	if (rc > 0) {
	    va = malloc(rc);
	    if (!va)
		abort();
	
	    rc = lgetxattr(argv[i], LINUX_NFS4_ACL_XATTR, va, rc);
	    printf("  lgetxattr(%s) -> %lld (%d=%s)\n",
		   argv[i], (long long int) rc, errno, strerror(errno));
	    
	    if (rc > 0) {
              rc = lsetxattr(argv[i], LINUX_NFS4_ACL_XATTR, va, rc, 0);
		printf("  lsetxattr(%s) -> %lld (%d=%s)\n",
		       argv[i], (long long int) rc, errno, strerror(errno));
	    }
	    free(va);
	}
#endif
	
	for (k = 0; a_flags[k].s; k++) {
	    printf("\n  -- %s:\n", a_flags[k].s);
	    
	    errno = 0;
	    a = acl_get_file(argv[i], a_flags[k].f);
	    printf("    acl_get_file(%s, %s) -> %p (%d=%s)\n",
		   argv[i], a_flags[k].s, a, errno, strerror(errno));
	    if (a) {
		check_acl(a, a_flags[k].f, a_flags[k].s, -1, argv[i]);
		
		errno = 0;
		rc = acl_set_file(argv[i], a_flags[k].f, a);
		printf("    acl_set_file(%s, %s) -> %p (%d=%s)\n",
		       argv[i], a_flags[k].s, a, errno, strerror(errno));
		acl_free(a);
	    }

#if defined(ACL_TYPE_NFS4) || defined(__APPLE__)
	    errno = 0;
	    a = acl_get_link_np(argv[i], a_flags[k].f);
	    printf("    acl_get_link_np(%s, %s) -> %p (%d=%s)\n",
		   argv[i], a_flags[k].s, a, errno, strerror(errno));
	    if (a) {
		check_acl(a, a_flags[k].f, a_flags[k].s, -1, argv[i]);
		
		errno = 0;
		rc = acl_set_link_np(argv[i], a_flags[k].f, a);
		printf("    acl_set_link_np(%s, %s) -> %p (%d=%s)\n",
		       argv[i], a_flags[k].s, a, errno, strerror(errno));
		acl_free(a);
	    }
#endif
	    
	    for (j = 0; o_flags[j].s; j++) {
		printf("\n    -- %s:\n", o_flags[j].s);

		errno = 0;
		fd = open(argv[i], o_flags[j].f);
		printf("      open(%s, %s) -> %d (%d=%s)\n",
		       argv[i], o_flags[j].s, fd, errno, strerror(errno));
		
		errno = 0;
		nfd = openat(dfd, argv[i], o_flags[j].f);
		printf("      openat(%d, %s, %s) -> %d (%d=%s)\n",
		       dfd, argv[i], o_flags[j].s, nfd, errno, strerror(errno));
		
		if (fd < 0 && nfd >= 0)
		    fd = nfd;
		
		if (fd > 0) {
		    errno = 0;
		    rc = fstat(fd, &sb);
		    printf("      fstat(%d=%s) -> %d (%d=%s)\n",
			   fd, argv[i], (int) rc, errno, strerror(errno));

		    if (rc == 0) {
			rc = fchmod(fd, sb.st_mode&ALLPERMS);
			printf("      fchmod(%d=%s, %04o) -_> %d (%d=%s)\n",
			       fd, argv[i], sb.st_mode&ALLPERMS, (int) rc, errno, strerror(errno));

#ifdef AT_EMPTY_PATH
                        errno = 0;	
                        rc = fchmodat(fd, "", sb.st_mode&ALLPERMS, AT_EMPTY_PATH);
			printf("      fchmodat(%d=%s, \"\", %04o, AT_EMPTY_PATH) -> %d (%d=%s)\n",
			       fd, argv[i], sb.st_mode&ALLPERMS, (int) rc, errno, strerror(errno));
#endif
		    }

#ifdef __linux__
		    errno = 0;
		    rc = fgetxattr(fd, LINUX_NFS4_ACL_XATTR, NULL, 0);
		    printf("      fgetxattr(%d=%s) -> %lld (%d=%s)\n",
			   fd, argv[i], (long long int) rc, errno, strerror(errno));
		    
		    if (rc > 0) {
			va = malloc(rc);
			if (!va)
			    abort();
			
			rc = fgetxattr(fd, LINUX_NFS4_ACL_XATTR, va, rc);
			printf("  fgetxattr(%d) -> %lld (%d=%s)\n",
			       fd, (long long int) rc, errno, strerror(errno));
			
			if (rc > 0) {
                            rc = fsetxattr(fd, LINUX_NFS4_ACL_XATTR, va, rc, 0);
			    printf("  fsetxattr(%d) -> %lld (%d=%s)\n",
				   fd, (long long int) rc, errno, strerror(errno));
			}
			free(va);
		    }
#endif
		    
		    errno = 0;
		    a = acl_get_fd(fd);
		    printf("      acl_get_fd(%d=%s) -> %p (%d=%s)\n",
			   fd, argv[i], a, errno, strerror(errno));
		    if (a) {
                        iputs(8, acl_to_text(a, NULL));
			check_acl(a, a_flags[k].f, a_flags[k].s, fd, argv[i]);
		
			errno = 0;
			rc = acl_set_fd(fd, a);
			printf("      acl_set_fd(%d=%s) -> %d (%d=%s)\n",
			       fd, argv[i], (int) rc, errno, strerror(errno));
			acl_free(a);
		    }
			
		    
#if defined(__APPLE__) || defined(ACL_TYPE_NFS4)
		    errno = 0;
		    a = acl_get_fd_np(fd, a_flags[k].f);
		    printf("      acl_get_fd_np(%d=%s, %s) -> %p (%d=%s)\n",
			   fd, argv[i], a_flags[k].s, a, errno, strerror(errno));
		    if (a) {
                        iputs(8, acl_to_text(a, NULL));
			check_acl(a, a_flags[k].f, a_flags[k].s, fd, argv[i]);
		    
			errno = 0;
			rc = acl_set_fd_np(fd, a, a_flags[k].f);
			printf("      acl_set_fd_np(%d=%s, %s) -> %d (%d=%s)\n",
			       fd, argv[i], a_flags[k].s, (int) rc, errno, strerror(errno));
			acl_free(a);
		    }
#endif
		    
		}
	    }
	}
    }
    return 0;
}
