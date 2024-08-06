/*
** digest.h - Digest/Checksum functions
**
** Copyright (c) 2020, Peter Eriksson <pen@lysator.liu.se>
** All rights reserved.
** 
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
** 
** 1. Redistributions of source code must retain the above copyright notice, this
**    list of conditions and the following disclaimer.
** 
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
** 
** 3. Neither the name of the copyright holder nor the names of its
**    contributors may be used to endorse or promote products derived from
**    this software without specific prior written permission.
** 
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DIGEST_H
#define DIGEST_H 1

#include "config.h"

#include <stdio.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include <sys/types.h>

#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif

#ifdef HAVE_MD5_H
#include <md5.h>
#endif
#ifdef HAVE_SKEIN_H
#include <skein.h>
#endif
#ifdef HAVE_SHA256_H
#include <sha256.h>
#endif
#ifdef HAVE_SHA384_H
#include <sha384.h>
#endif
#ifdef HAVE_SHA512_H
#include <sha512.h>
#endif



typedef struct fletcher16_ctx {
    uint32_t c0;
    uint32_t c1;
} FLETCHER16_CTX;

typedef struct fletcher32_ctx {
    uint32_t c0;
    uint32_t c1;
} FLETCHER32_CTX;


typedef enum {
	      DIGEST_TYPE_INVALID    = -1,
	      DIGEST_TYPE_NONE       =  0,
              DIGEST_TYPE_FLETCHER16 =  1,
              DIGEST_TYPE_FLETCHER32 =  2,

              /* This one is a simple XOR of all bytes in a file and is really bad, don't use */
	      DIGEST_TYPE_XOR8       = 11,
              
#ifdef HAVE_ADLER32_Z
	      DIGEST_TYPE_ADLER32    = 21,
#endif
#ifdef HAVE_CRC32_Z
	      DIGEST_TYPE_CRC32      = 22,
#endif
#ifdef HAVE_MD5INIT
	      DIGEST_TYPE_MD5        = 31,
#endif
#ifdef HAVE_SKEIN256_INIT
	      DIGEST_TYPE_SKEIN256   = 41,
#endif
#ifdef HAVE_SHA256_INIT
	      DIGEST_TYPE_SHA256     = 51,
#endif
#ifdef HAVE_SHA384_INIT
	      DIGEST_TYPE_SHA384     = 52,
#endif
#ifdef HAVE_SHA512_INIT
	      DIGEST_TYPE_SHA512     = 53,
#endif
} DIGEST_TYPE;

typedef struct {
    char *name;
    DIGEST_TYPE type;
} DIGEST_LIST;

typedef enum {
	      DIGEST_STATE_NONE    = 0,
	      DIGEST_STATE_INIT    = 1,
	      DIGEST_STATE_UPDATE  = 2,
	      DIGEST_STATE_FINAL   = 3,
} DIGEST_STATE;


typedef struct digest {
    DIGEST_TYPE  type;
    DIGEST_STATE state;
    union {
        FLETCHER16_CTX fletcher16;
        FLETCHER32_CTX fletcher32;
        uint8_t        xor8;

#ifdef HAVE_ADLER32_Z
        uint32_t       adler32;
#endif
#ifdef HAVE_CRC32_Z        
        uint32_t       crc32;
#endif
        
#ifdef HAVE_MD5INIT
        MD5_CTX        md5;
#endif
#ifdef HAVE_SKEIN256_INIT
        SKEIN256_CTX   skein256;
#endif
#ifdef HAVE_SHA256_INIT
        SHA256_CTX     sha256;
#endif
#ifdef HAVE_SHA384_INIT
        SHA384_CTX     sha384;
#endif
#ifdef HAVE_SHA512_INIT
        SHA512_CTX     sha512;
#endif
    } ctx;
} DIGEST;


/*
 * Result buffer sizes
 */
#define DIGEST_BUFSIZE_XOR8        sizeof(uint8_t)
#define DIGEST_BUFSIZE_FLETCHER16  sizeof(uint16_t)
#define DIGEST_BUFSIZE_FLETCHER32  sizeof(uint32_t)
#define DIGEST_BUFSIZE_ADLER32     sizeof(uint32_t)
#define DIGEST_BUFSIZE_CRC32       sizeof(uint32_t)
#define DIGEST_BUFSIZE_MD5         16
#define DIGEST_BUFSIZE_SKEIN256    32
#define DIGEST_BUFSIZE_SHA256      32
#define DIGEST_BUFSIZE_SHA384      48  
#define DIGEST_BUFSIZE_SHA512      64

#define DIGEST_BUFSIZE_MAX         64


extern int
digest_init(DIGEST *dp,
            DIGEST_TYPE type);

extern void
digest_destroy(DIGEST *dp);

extern int
digest_update(DIGEST *dp,
	      const unsigned char *buf,
	      size_t bufsize);

extern ssize_t
digest_final(DIGEST *dp,
	     unsigned char *buf,
	     size_t bufsize);


extern DIGEST_TYPE
digest_typeof(DIGEST *dp);

extern DIGEST_STATE
digest_stateof(DIGEST *dp);


extern DIGEST_TYPE
digest_str2type(const char *str);

extern const char *
digest_type2str(DIGEST_TYPE type);

extern void
digests_print(FILE *fp,
	      const char *sep);

#endif
