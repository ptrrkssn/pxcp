/*
 * acls.h
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

#ifndef PXCP_ACLS_H
#define PXCP_ACLS_H 1

#include "config.h"
#include "fsobj.h"

#include <sys/types.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#if HAVE_SYS_ACL_H
#include <sys/acl.h>
#endif

#if HAVE_ACL_LIBACL_H
#include <acl/libacl.h>
#endif



#if !HAVE_ACL_GET_PERM_NP && HAVE_ACL_GET_PERM
#define acl_get_perm_np(ps,p) acl_get_perm(ps,p)
#endif

#ifndef ACL_TYPE_NFS4
# if defined(__DARWIN_ACL_EXTENDED_ALLOW)
/* MacOS */
#  define ACL_TYPE_NFS4 ACL_TYPE_EXTENDED

# elif defined(__linux__) && defined(HAVE_FGETXATTR)
/* Linux */
#   define GACL_TYPE_NFS4  (0x10000000)
#   define ACL_TYPE_NFS4   GACL_TYPE_NFS4

# elif defined(HAVE_FACL) && defined(ACE_ACCESS_ALLOWED_ACE_TYPE)
/* Solaris */

typedef int acl_perm_t;

#  define ACL_BRAND_UNKNOWN 0
#  define ACL_BRAND_POSIX   1
#  define ACL_BRAND_NFS4    2

#  define GACL_TYPE_NFS4 (0x1000000)
#  define GACL_TYPE_UFS  (0x1000001)
#  define ACL_TYPE_NFS4   GACL_TYPE_NFS4
#  define ACL_TYPE_UFS    GACL_TYPE_UFS

#  define ACL_ENTRY_TYPE_ALLOW     ACE_ACCESS_ALLOWED_ACE_TYPE
#  define ACL_ENTRY_TYPE_DENY      ACE_ACCESS_DENIED_ACE_TYPE
#  define ACL_ENTRY_TYPE_AUDIT     ACE_SYSTEM_AUDIT_ACE_TYPE
#  define ACL_ENTRY_TYPE_ALARM     ACE_SYSTEM_ALARM_ACE_TYPE

#  define ACL_USER_OBJ             ACE_OWNER
#  define ACL_USER                 0
#  define ACL_GROUP_OBJ            ACE_GROUP
#  define ACL_GROUP                ACE_IDENTIFIER_GROUP
#  define ACL_EVERYONE             ACE_EVERYONE

#  define ACL_READ_DATA            ACE_READ_DATA
#  define ACL_LIST_DIRECTORY       ACE_LIST_DIRECTORY
#  define ACL_WRITE_DATA           ACE_WRITE_DATA
#  define ACL_ADD_FILE             ACE_ADD_FILE
#  define ACL_APPEND_DATA          ACE_APPEND_DATA
#  define ACL_ADD_SUBDIRECTORY     ACE_ADD_SUBDIRECTORY
#  define ACL_READ_NAMED_ATTRS     ACE_READ_NAMED_ATTRS
#  define ACL_WRITE_NAMED_ATTRS    ACE_WRITE_NAMED_ATTRS
#  define ACL_EXECUTE              ACE_EXECUTE
#  define ACL_DELETE_CHILD         ACE_DELETE_CHILD
#  define ACL_READ_ATTRIBUTES      ACE_READ_ATTRIBUTES
#  define ACL_WRITE_ATTRIBUTES     ACE_WRITE_ATTRIBUTES
#  define ACL_DELETE               ACE_DELETE
#  define ACL_READ_ACL             ACE_READ_ACL
#  define ACL_WRITE_ACL            ACE_WRITE_ACL
#  define ACL_WRITE_OWNER          ACE_WRITE_OWNER
#  define ACL_SYNCHRONIZE          ACE_SYNCHRONIZE

#  define ACL_ENTRY_FILE_INHERIT            ACE_FILE_INHERIT_ACE
#  define ACL_ENTRY_DIRECTORY_INHERIT       ACE_DIRECTORY_INHERIT_ACE
#  define ACL_ENTRY_NO_PROPAGATE_INHERIT    ACE_NO_PROPAGATE_INHERIT_ACE
#  define ACL_ENTRY_INHERIT_ONLY            ACE_INHERIT_ONLY_ACE
#  define ACL_ENTRY_INHERITED               ACE_INHERITED_ACE

# endif
#endif


#define GACL_MAGIC 0x984ae24f

typedef struct gacl {
  unsigned int m;
  unsigned int t;
  size_t s;
  void *a;
} GACL;


extern int
gacl_get(GACL *ga,
         FSOBJ *op,
         unsigned int t);

extern int
gacl_set(FSOBJ *op,
         GACL *ga);

extern int
gacl_diff(GACL *src,
	  GACL *dst);

extern void
gacl_free(GACL *ga);

#endif
