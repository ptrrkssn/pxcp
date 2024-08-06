/*
** digest.h - Digest/Checksum functions
**
** Copyright (c) 2024, Peter Eriksson <pen@lysator.liu.se>
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

#include "digest.h"

#include <errno.h>
#include <string.h>

#include <arpa/inet.h>


DIGEST_LIST digests[] = {
    { "NONE",       DIGEST_TYPE_NONE },
    { "FLETCHER16", DIGEST_TYPE_FLETCHER16 },
#ifdef HAVE_ADLER32_Z
    { "ADLER32",    DIGEST_TYPE_ADLER32 },
#endif
#ifdef HAVE_CRC32_Z
    { "CRC32",      DIGEST_TYPE_CRC32 },
#endif
#ifdef HAVE_MD5INIT
    { "MD5",        DIGEST_TYPE_MD5 },
#endif
#ifdef HAVE_SKEIN256_INIT
    { "SKEIN256",   DIGEST_TYPE_SKEIN256 },
#endif
#ifdef HAVE_SHA256_INIT
    { "SHA256",     DIGEST_TYPE_SHA256 },
#endif
#ifdef HAVE_SHA384_INIT
    { "SHA384",     DIGEST_TYPE_SHA384 },
#endif
#ifdef HAVE_SHA512_INIT
    { "SHA512",     DIGEST_TYPE_SHA512 },
#endif
    { "XOR8",       DIGEST_TYPE_XOR8 },
    { NULL,         DIGEST_TYPE_INVALID },
};





DIGEST_TYPE
digest_str2type(const char *s) {
    int i;

    for (i = 0; digests[i].name && strcasecmp(s, digests[i].name) != 0; i++)
	;

    return digests[i].type;
}


const char *
digest_type2str(DIGEST_TYPE type) {
    int i;

    for (i = 0; digests[i].name && type != digests[i].type; i++)
	;
    return digests[i].name;
}


void
digests_print(FILE *fp,
	      const char *sep) {
    int i;

    for (i = 0; digests[i].name; i++) {
	if (i > 0)
	    fputs(sep, fp);
	fprintf(fp, "%s(%d)", digests[i].name, i);
    }
    putc('\n', fp);
}

static void
fletcher16_init(FLETCHER16_CTX *ctx) {
    ctx->c0 = 0;
    ctx->c1 = 0;
}

static void
fletcher16_update(FLETCHER16_CTX *ctx,
                  const uint8_t *data,
                  size_t len) {
    if (!data)
        return;

    while (len > 0) {
        size_t blocklen = len;
        
        if (blocklen > 5802) {
            blocklen = 5802;
        }
        len -= blocklen;

        do {
            ctx->c0 += *data++;
            ctx->c1 += ctx->c0;
        } while (--blocklen);
        ctx->c0 %= 255;
        ctx->c1 %= 255;
    }
}

static uint16_t
fletcher16_final(FLETCHER16_CTX *ctx) {
    return (ctx->c1 << 8 | ctx->c0);
}



int
digest_init(DIGEST *dp,
            DIGEST_TYPE type) {
    memset(dp, 0, sizeof(*dp));
    
    dp->state = DIGEST_STATE_NONE;
    dp->type = type;
    
    switch (dp->type) {
    case DIGEST_TYPE_NONE:
        break;
        
    case DIGEST_TYPE_XOR8:
        dp->ctx.xor8 = 0;
        break;
        
    case DIGEST_TYPE_FLETCHER16:
        fletcher16_init(&dp->ctx.fletcher16);
        break;
        
#ifdef HAVE_ADLER32_Z
    case DIGEST_TYPE_ADLER32:
        dp->ctx.crc32 = adler32_z(0L, NULL, 0);
        break;
#endif
        
#ifdef HAVE_CRC32_Z
    case DIGEST_TYPE_CRC32:
        dp->ctx.adler32 = crc32_z(0L, NULL, 0);
        break;
#endif
        
#ifdef HAVE_MD5INIT
    case DIGEST_TYPE_MD5:
        MD5Init(&dp->ctx.md5);
        break;
#endif
        
#ifdef HAVE_SKEIN256_INIT
    case DIGEST_TYPE_SKEIN256:
        SKEIN256_Init(&dp->ctx.skein256);
        break;
#endif
        
#ifdef HAVE_SHA256_INIT
    case DIGEST_TYPE_SHA256:
        SHA256_Init(&dp->ctx.sha256);
        break;
#endif
        
#ifdef HAVE_SHA384_INIT
    case DIGEST_TYPE_SHA384:
        SHA384_Init(&dp->ctx.sha384);
        break;
#endif
        
#ifdef HAVE_SHA512_INIT
    case DIGEST_TYPE_SHA512:
        SHA512_Init(&dp->ctx.sha512);
        break;
#endif
        
    default:
        return -1;
    }
    
    dp->state = DIGEST_STATE_INIT;
    return 0;
}



int
digest_update(DIGEST *dp,
	      const unsigned char *buf,
	      size_t bufsize) {

    if (!dp)
        return -1;

    switch (dp->state) {
    case DIGEST_STATE_INIT:
    case DIGEST_STATE_UPDATE:
        switch (dp->type) {
      
        case DIGEST_TYPE_XOR8:
            while (bufsize-- > 0)
                dp->ctx.xor8 |= *buf++;
            break;

        case DIGEST_TYPE_FLETCHER16:
            fletcher16_update(&dp->ctx.fletcher16, buf, bufsize);
            break;
            
#ifdef HAVE_ADLER32_Z
        case DIGEST_TYPE_ADLER32:
            dp->ctx.adler32 = adler32_z(dp->ctx.adler32, buf, bufsize);
            break;
#endif

#ifdef HAVE_CRC32_Z
        case DIGEST_TYPE_CRC32:
            dp->ctx.adler32 = crc32_z(dp->ctx.crc32, buf, bufsize);
            break;
#endif

#ifdef HAVE_MD5INIT
        case DIGEST_TYPE_MD5:
            MD5Update(&dp->ctx.md5, buf, bufsize);
            break;
#endif

#ifdef HAVE_SKEIN256_INIT
        case DIGEST_TYPE_SKEIN256:
            SKEIN256_Update(&dp->ctx.skein256, buf, bufsize);
            break;
#endif

#ifdef HAVE_SHA256_INIT
        case DIGEST_TYPE_SHA256:
            SHA256_Update(&dp->ctx.sha256, buf, bufsize);
            break;
#endif

#ifdef HAVE_SHA384_INIT
        case DIGEST_TYPE_SHA384:
            SHA384_Update(&dp->ctx.sha384, buf, bufsize);
            break;
#endif

#ifdef HAVE_SHA512_INIT
        case DIGEST_TYPE_SHA512:
            SHA512_Update(&dp->ctx.sha512, buf, bufsize);
            break;
#endif
      
        default:
            return -1;
        }
        break;
    
    default:
        return -1;
    }
  
    dp->state = DIGEST_STATE_UPDATE;
    return 0;
}

ssize_t
digest_final(DIGEST *dp,
	     unsigned char *buf,
	     size_t bufsize) {
    ssize_t rlen = -1;
    
    switch (dp->state) {
    case DIGEST_STATE_NONE:
	return -1;
	
    case DIGEST_STATE_FINAL:
    case DIGEST_STATE_INIT:
    case DIGEST_STATE_UPDATE:
	switch (dp->type) {
	case DIGEST_TYPE_INVALID:
	    return -1;
	    
	case DIGEST_TYPE_NONE:
	    rlen = 0;
	    break;
	    
	case DIGEST_TYPE_XOR8:
	    if (bufsize < DIGEST_BUFSIZE_XOR8) {
		errno = EOVERFLOW;
		return -1;
	    }
	    * (uint8_t *) buf = dp->ctx.xor8;
	    rlen = DIGEST_BUFSIZE_XOR8;
	    break;

        case DIGEST_TYPE_FLETCHER16:
	    if (bufsize < DIGEST_BUFSIZE_FLETCHER16) {
		errno = EOVERFLOW;
		return -1;
	    }
	    * (uint16_t *) buf = htons(fletcher16_final(&dp->ctx.fletcher16));
	    rlen = DIGEST_BUFSIZE_FLETCHER16;
            break;
            
#ifdef HAVE_ADLER32_Z
	case DIGEST_TYPE_ADLER32:
	    if (bufsize < DIGEST_BUFSIZE_ADLER32) {
		errno = EOVERFLOW;
		return -1;
	    }
	    * (uint32_t *) buf = htonl(dp->ctx.adler32);
	    rlen = DIGEST_BUFSIZE_ADLER32;
	    break;
#endif
	    
#ifdef HAVE_CRC32_Z
	case DIGEST_TYPE_CRC32:
	    if (bufsize < DIGEST_BUFSIZE_CRC32) {
		errno = EOVERFLOW;
		return -1;
	    }
	    * (uint32_t *) buf = htonl(dp->ctx.crc32);
	    rlen = DIGEST_BUFSIZE_CRC32;
	    break;
#endif
	    
#ifdef HAVE_MD5INIT
	case DIGEST_TYPE_MD5:
	    if (bufsize < DIGEST_BUFSIZE_MD5) {
		errno = EOVERFLOW;
		return -1;
	    }
	    MD5Final(buf, &dp->ctx.md5);
	    rlen = DIGEST_BUFSIZE_MD5;
	    break;
#endif
	    
#ifdef HAVE_SKEIN256_INIT
	case DIGEST_TYPE_SKEIN256:
	    if (bufsize < DIGEST_BUFSIZE_SKEIN256) {
		errno = EOVERFLOW;
		return -1;
	    }
	    SKEIN256_Final(buf, &dp->ctx.skein256);
	    rlen = DIGEST_BUFSIZE_SKEIN256;
	    break;
#endif
	    
#ifdef HAVE_SHA256_INIT
	case DIGEST_TYPE_SHA256:
	    if (bufsize < DIGEST_BUFSIZE_SHA256) {
		errno = EOVERFLOW;
		return -1;
	    }
	    SHA256_Final(buf, &dp->ctx.sha256);
	    rlen = DIGEST_BUFSIZE_SHA256;
	    break;
#endif
	    
#ifdef HAVE_SHA384_INIT
	case DIGEST_TYPE_SHA384:
	    if (bufsize < DIGEST_BUFSIZE_SHA384) {
		errno = EOVERFLOW;
		return -1;
	    }
	    SHA384_Final(buf, &dp->ctx.sha384);
	    rlen = DIGEST_BUFSIZE_SHA384;
	    break;
#endif
	    
#ifdef HAVE_SHA512_INIT
	case DIGEST_TYPE_SHA512:
	    if (bufsize < DIGEST_BUFSIZE_SHA512) {
		errno = EOVERFLOW;
		return -1;
	    }
	    SHA512_Final(buf, &dp->ctx.sha512);
	    rlen = DIGEST_BUFSIZE_SHA512;
	    break;
#endif
	}
	break;
	
    default:
	errno = EINVAL;
	return -1;
    }
    
    dp->state = DIGEST_STATE_FINAL;
    return rlen;
}


void
digest_destroy(DIGEST *dp) {
    unsigned char tbuf[DIGEST_BUFSIZE_MAX];

    (void) digest_final(dp, tbuf, sizeof(tbuf));
    memset(dp, 0, sizeof(*dp));
    dp->state = DIGEST_STATE_NONE;
    dp->type  = DIGEST_TYPE_NONE;
}

DIGEST_TYPE
digest_typeof(DIGEST *dp) {
    if (!dp)
        return DIGEST_TYPE_NONE;
  
    return dp->type;
}

DIGEST_STATE
digest_stateof(DIGEST *dp) {
    if (!dp)
        return DIGEST_STATE_NONE;
  
    return dp->state;
}


