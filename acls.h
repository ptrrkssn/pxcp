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
# ifdef __DARWIN_ACL_EXTENDED_ALLOW
#  define ACL_TYPE_NFS4 ACL_TYPE_EXTENDED
# else
#  if defined(__linux__) && defined(HAVE_FGETXATTR)
#   define GACL_MAGIC_NFS4 (0x10000000)
#   define ACL_TYPE_NFS4   GACL_MAGIC_NFS4
#  endif
# endif
#endif


typedef struct gacl {
  acl_type_t t;
  size_t s;
  void *a;
} GACL;


extern int
gacl_get(GACL *ga,
         FSOBJ *op,
         acl_type_t t);

extern int
gacl_set(FSOBJ *op,
         GACL *ga);

extern int
gacl_diff(GACL *src,
	  GACL *dst);

extern void
gacl_free(GACL *ga);

#endif
