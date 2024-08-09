/*
 * acls.c
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
#include "acls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef __DARWIN_ACL_EXTENDED_ALLOW
#include <membership.h>
#endif


#define LINUX_NFS4_ACL_XATTR "system.nfs4_acl"


#if defined(ACL_EXECUTE) && defined(ACL_WRITE) && defined(ACL_READ)
static acl_perm_t acl_perms_posix[] = {
    ACL_EXECUTE,
    ACL_WRITE,
    ACL_READ
};
#endif

#ifndef HAVE_FACL
# ifdef ACL_READ_DATA
static acl_perm_t acl_perms_nfs4[] = {
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
    ACL_WRITE_OWNER,
    ACL_SYNCHRONIZE
};
# endif

# ifdef ACL_ENTRY_FILE_INHERIT
static int acl_flags_nfs4[] = {
    ACL_ENTRY_FILE_INHERIT,
    ACL_ENTRY_DIRECTORY_INHERIT,
    ACL_ENTRY_NO_PROPAGATE_INHERIT,
    ACL_ENTRY_INHERIT_ONLY,
    ACL_ENTRY_INHERITED
};
# endif
#endif


extern int f_debug;


#ifdef HAVE_ACL_GET_ENTRY
static int
_acl_diff(acl_t src,
          acl_t dst) {
    int d_rc, s_rc;
#ifndef __DARWIN_ACL_EXTENDED_ALLOW
    int j;
#endif
    acl_entry_t s_e, d_e;


#if HAVE_ACL_GET_BRAND_NP
    int s_b, d_b;

    /* Check brand */
    if (acl_get_brand_np(src, &s_b) < 0)
	return -1;
    if (acl_get_brand_np(dst, &d_b) < 0)
	return -1;
    if (s_b != d_b)
	return 1;
#endif

    s_rc = acl_get_entry(src, ACL_FIRST_ENTRY, &s_e);
    d_rc = acl_get_entry(dst, ACL_FIRST_ENTRY, &d_e);
    while (s_rc > 0 && d_rc > 0) {
	acl_permset_t s_ps, d_ps;
	acl_tag_t s_t, d_t;
	void *s_q, *d_q;
	uid_t s_ugid, d_ugid;


	/* OWNER@, GROUP@, user:, group:, EVERYONE@ etc */
	if (acl_get_tag_type(s_e, &s_t) < 0) {
	  if (f_debug)
	    fprintf(stderr, "acl_get_tag_type failed: %s\n", strerror(errno));;
	    return -1;
	}
	if (acl_get_tag_type(d_e, &d_t) < 0)
	    return -1;
	if (s_t != d_t)
	    return 2;

	switch (s_t) {
#ifdef ACL_USER_OBJ
        case ACL_USER_OBJ:
          break;
#endif
#ifdef ACL_GROUP_OBJ
        case ACL_GROUP_OBJ:
          break;
#endif
#ifdef ACL_MASK
        case ACL_MASK:
          break;
#endif
#ifdef ACL_OTHER
        case ACL_OTHER:
          break;
#endif
#ifdef ACL_EVERYONE
        case ACL_EVERYONE:
          break;
#endif
#ifdef ACL_USER
	case ACL_USER:
	    s_q = acl_get_qualifier(s_e);
	    if (!s_q)
		return -1;
	    d_q = acl_get_qualifier(s_e);
	    if (!d_q) {
		acl_free(s_q);
		return -1;
	    }
	    s_ugid = * (uid_t *) s_q;
	    d_ugid = * (uid_t *) d_q;
	    acl_free(s_q);
	    acl_free(d_q);
	    if (s_ugid != d_ugid)
		return 3;
	    break;
#endif
#ifdef ACL_GROUP
	case ACL_GROUP:
	    s_q = acl_get_qualifier(s_e);
	    if (!s_q)
		return -1;
	    d_q = acl_get_qualifier(s_e);
	    if (!d_q) {
		acl_free(s_q);
		return -1;
	    }
	    s_ugid = * (gid_t *) s_q;
	    d_ugid = * (gid_t *) d_q;
	    acl_free(s_q);
	    acl_free(d_q);
	    if (s_ugid != d_ugid)
		return 4;
	    break;
#endif
#ifdef __DARWIN_ACL_EXTENDED_ALLOW
	case ACL_EXTENDED_ALLOW:
	case ACL_EXTENDED_DENY:
	    /* MacOS */
	    {
	    int s_ugtype, d_ugtype;

	    s_q = acl_get_qualifier(s_e);
	    if (mbr_uuid_to_id((const unsigned char *) s_q, &s_ugid, &s_ugtype) < 0)
	        return -1;
	    d_q = acl_get_qualifier(d_e);
	    if (mbr_uuid_to_id((const unsigned char *) d_q, &d_ugid, &d_ugtype) < 0)
	        return -1;
	    acl_free(s_q);
	    acl_free(d_q);
	    if (s_ugtype != d_ugtype || s_ugid != d_ugid)
	        return 3;
	    }
	    break;
#endif
	default:
	  if (f_debug)
	    fprintf(stderr, "acl_get_tag_type: Unhandled type: %d\n", s_t);
	  return -1;
	}

	/* Check permset */
	if (acl_get_permset(s_e, &s_ps) < 0)
	    return -1;
	if (acl_get_permset(d_e, &d_ps) < 0)
	    return -1;

#if defined(ACL_BRAND_NFS4) || defined(ACL_BRAND_POSIX)
	switch (s_b) {
#ifdef ACL_BRAND_POSIX
	case ACL_BRAND_POSIX:
	    for (j = 0; j < sizeof(acl_perms_posix)/sizeof(acl_perms_posix[0]); j++) {
		acl_perm_t s_p, d_p;

		s_p = acl_get_perm_np(s_ps, acl_perms_posix[j]);
		d_p = acl_get_perm_np(d_ps, acl_perms_posix[j]);
		if (s_p != d_p)
		    return 5;
	    }
	    break;
#endif
#ifdef ACL_BRAND_NFS4
	case ACL_BRAND_NFS4:
	    for (j = 0; j < sizeof(acl_perms_nfs4)/sizeof(acl_perms_nfs4[0]); j++) {
		acl_perm_t s_p, d_p;

		s_p = acl_get_perm_np(s_ps, acl_perms_nfs4[j]);
		d_p = acl_get_perm_np(d_ps, acl_perms_nfs4[j]);
		if (s_p != d_p)
		    return 6;
	    }
	    break;
#endif
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

		s_f = acl_get_flag_np(s_fs, acl_flags_nfs4[j]);
		d_f = acl_get_flag_np(d_fs, acl_flags_nfs4[j]);
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
#else
#if defined(ACL_EXECUTE) && defined(ACL_READ) && defined(ACL_WRITE)
	for (j = 0; j < sizeof(acl_perms_posix)/sizeof(acl_perms_posix[0]); j++) {
	    acl_perm_t s_p, d_p;

	    s_p = acl_get_perm_np(s_ps, acl_perms_posix[j]);
	    d_p = acl_get_perm_np(d_ps, acl_perms_posix[j]);
	    if (s_p != d_p)
		return 5;
	}
#endif
#endif

	s_rc = acl_get_entry(src, ACL_NEXT_ENTRY, &s_e);
	d_rc = acl_get_entry(dst, ACL_NEXT_ENTRY, &d_e);
    }

    if (s_rc != d_rc)
	return 9;

    return 0;
}
#endif



void
gacl_init(GACL *ga) {
  memset(ga, 0, sizeof(*ga));
  ga->m = GACL_MAGIC;
}


int
gacl_diff(GACL *src,
          GACL *dst) {
    int d;

    d = src->t - dst->t;
    if (d)
        return d;

#if defined(HAVE_FGETXATTR) && defined(__linux__)
    if (src->t == ACL_TYPE_NFS4) {
        d = src->s - dst->s;
        if (d)
            return d;
      
        return memcmp(src->a, dst->a, src->s);
    }
#elif defined(HAVE_FACL)
    d = src->s - dst->s;
    if (d)
        return d;
    
    switch (src->t) {
    case ACL_TYPE_NFS4:
        return memcmp(src->a, dst->a, src->s*sizeof(ace_t));
#ifdef ACL_TYPE_UFS
    case ACL_TYPE_UFS:
        return memcmp(src->a, dst->a, src->s*sizeof(aclent_t));
#endif
    default:
        return -1;
    }
#else

# ifdef HAVE_ACL_GET_ENTRY
    return _acl_diff(src->a, dst->a);
# else
    return 0;
# endif
#endif
}


int
gacl_get(GACL *ga,
         FSOBJ *op,
         unsigned int t) {
    void *a = NULL;
    ssize_t rc = 0, s = -1;

    
    if (!ga)
        abort();
    
#if defined(HAVE_FGETXATTR) && defined(__linux__)
    if (op->fd >= 0 && t == ACL_TYPE_NFS4) {
        rc = fgetxattr(op->fd, LINUX_NFS4_ACL_XATTR, NULL, 0);
        if (rc > 0) {
            a = malloc(rc);
            if (!a)
	        return -1;

            s = fgetxattr(op->fd, LINUX_NFS4_ACL_XATTR, a, rc);
	    if (s < 0) {
                free(a);
                return -1;
            } else if (s == 0) {
	        free(a);
		a = NULL;
	    }
        } else
	    s = 0;
	
	goto End;
    }
#elif defined(HAVE_FACL)
    if (op->fd >= 0) {
        switch (t) {
	case ACL_TYPE_NFS4:
	    rc = facl(op->fd, ACE_GETACLCNT, 0, NULL);
	    if (rc < 0)
	        return -1;
	    if (rc > 0) {
	        a = calloc(rc, sizeof(ace_t));
		if (!a)
		    return -1;
		s = facl(op->fd, ACE_GETACL, rc, a);
		if (s < 0) {
		    free(a);
		    return -1;
		} else if (s == 0) {
		    free(a);
		    a = NULL;
		}
	    } else
	        s = 0;
	    break;
	    
# ifdef ACL_TYPE_UFS
	case ACL_TYPE_UFS:
	    rc = facl(op->fd, GETACLCNT, 0, NULL);
	    if (rc < 0)
	        return -1;
	    if (rc > 0) {
	        a = calloc(rc, sizeof(aclent_t));
		if (!a)
		    return -1;
		s = facl(op->fd, GETACL, rc, a);
		if (s < 0) {
		    free(a);
		    return -1;
		} else if (s == 0) {
		    free(a);
		    a = NULL;
		}
	    } else
	        s = 0;
	    break;
# endif
	default:
	  errno = EINVAL;
	  return -1;
	}
	goto End;
    }	
#else
# if HAVE_ACL_GET_FD_NP
    if (op->fd >= 0) {
        a = acl_get_fd_np(op->fd, t);
        goto End;
    }
# endif
# if HAVE_ACL_GET_LINK_NP
    a = acl_get_link_np(fsobj_path(op), t);
    goto End;
# else
#  if HAVE_ACL_GET_FD
    if (op->fd >= 0 && (op->flags & O_PATH) == 0 && t == ACL_TYPE_ACCESS) {
        a = acl_get_fd(op->fd);
        goto End;
    }
#  endif
#  if HAVE_ACL_GET_FILE
    a = acl_get_file(fsobj_path(op), t);
    goto End;
#  else
    errno = ENOSYS;
    return -1;
#  endif
# endif
#endif

 End:
    gacl_init(ga);
    ga->a = a;
    ga->s = s;
    ga->t = t;
    
    return rc;
}


int
gacl_set(FSOBJ *op,
         GACL *ga) {
    if (ga->m != GACL_MAGIC)
        abort();

    
#if defined(HAVE_FSETXATTR) && defined(__linux__)
    if (ga->t == ACL_TYPE_NFS4) {
        int rc;

        if (op->fd >= 0)
            rc = fsetxattr(op->fd, LINUX_NFS4_ACL_XATTR, ga->a, ga->s, 0);
        else
            rc = lsetxattr(fsobj_path(op), LINUX_NFS4_ACL_XATTR, ga->a, ga->s, 0);
        return rc;
    }
#elif defined(HAVE_FACL)
    if (ga->s < 0)
        return -1;
    
    if (op->fd >= 0) {
        switch (ga->t) {
	case ACL_TYPE_NFS4:
	    return facl(op->fd, ACE_SETACL, ga->s, ga->a);
	  
# ifdef ACL_TYPE_UFS
	case ACL_TYPE_UFS:
	    return facl(op->fd, SETACL, ga->s, ga->a);
# endif
	    
	default:
	    errno = EINVAL;
	    return -1;
	}
    }
    
    switch (ga->t) {
    case ACL_TYPE_NFS4:
        return acl(fsobj_path(op), ACE_SETACL, ga->s, ga->a);

# ifdef ACL_TYPE_UFS
    case ACL_TYPE_UFS:
        return acl(fsobj_path(op), SETACL, ga->s, ga->a);
# endif
	
    default:
       errno = EINVAL;
       return -1;
    }
#else

# if HAVE_ACL_SET_FD_NP
    if (op->fd >= 0 && (op->flags & O_PATH) == 0)
        return acl_set_fd_np(op->fd, ga->a, ga->t);
# endif
# if HAVE_ACL_SET_LINK_NP
    return acl_set_link_np(fsobj_path(op), ga->t, ga->a);
# else
#  if HAVE_ACL_SET_FD
    if (op->fd >= 0 && (op->flags & O_PATH) == 0 && ga->t == ACL_TYPE_ACCESS)
        return acl_set_fd(op->fd, ga->a);
#  endif
#  if HAVE_ACL_SET_FILE
    return acl_set_file(fsobj_path(op), ga->t, ga->a);
#  else
    errno = ENOSYS;
    return -1;
#  endif
# endif
#endif
}


void
gacl_free(GACL *ga) {
    if (!ga)
        return;
    if (ga->m != GACL_MAGIC)
        abort();
    
#if defined(HAVE_FGETXATTR) && defined(__linux__)
    if (ga->a)
        free(ga->a);
#elif defined(HAVE_FACL)
    if (ga->a)
        free(ga->a);
#else
    if (ga->a)
        acl_free(ga->a);
#endif
    
    memset(ga, 0, sizeof(*ga));
}
