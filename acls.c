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

#include "acls.h"

#ifdef ACL_EXECUTE
static acl_perm_t acl_perms_posix[] = {
    ACL_EXECUTE,
    ACL_WRITE,
    ACL_READ
};
#endif

#ifdef ACL_READ_DATA
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
    ACL_SYNCHRONIZE
};
#endif

#ifdef ACL_ENTRY_FILE_INHERIT
static int acl_flags_nfs4[] = {
    ACL_ENTRY_FILE_INHERIT,
    ACL_ENTRY_DIRECTORY_INHERIT,
    ACL_ENTRY_NO_PROPAGATE_INHERIT,
    ACL_ENTRY_INHERIT_ONLY,
    ACL_ENTRY_INHERITED
};
#endif


int
acl_diff(acl_t src,
	 acl_t dst) {
    int j, d_rc, s_rc;
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

#ifdef ACL_BRAND_NFS4
	switch (s_b) {
	case ACL_BRAND_POSIX:
	    for (j = 0; j < sizeof(acl_perms_posix)/sizeof(acl_perms_posix[0]); j++) {
		acl_perm_t s_p, d_p;

		s_p = acl_get_perm_np(s_ps, acl_perms_posix[j]);
		d_p = acl_get_perm_np(d_ps, acl_perms_posix[j]);
		if (s_p != d_p)
		    return 5;
	    }
	    break;
	case ACL_BRAND_NFS4:
	    for (j = 0; j < sizeof(acl_perms_nfs4)/sizeof(acl_perms_nfs4[0]); j++) {
		acl_perm_t s_p, d_p;

		s_p = acl_get_perm_np(s_ps, acl_perms_nfs4[j]);
		d_p = acl_get_perm_np(d_ps, acl_perms_nfs4[j]);
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
#ifdef ACL_EXECUTE
	for (j = 0; j < sizeof(acl_perms_posix)/sizeof(acl_perms_posix[0]); j++) {
	    acl_perm_t s_p, d_p;

	    s_p = acl_get_perm_np(s_ps, acl_perms_posix[j]);
	    d_p = acl_get_perm_np(d_ps, acl_perms_posix[j]);
	    if (s_p != d_p)
		return 5;
	}
	break;
#endif
#endif

	s_rc = acl_get_entry(src, ACL_NEXT_ENTRY, &s_e);
	d_rc = acl_get_entry(dst, ACL_NEXT_ENTRY, &d_e);
    }

    if (s_rc != d_rc)
	return 9;

    return 0;
}
