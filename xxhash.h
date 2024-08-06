/*
** xxhash.h - XXHASH Digest/Checksum functions
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

#ifndef PXCP_XXHASH_H
#define PXCP_XXHASH_H 1

#include <stdint.h>
#include <sys/types.h>

typedef struct xxhash32_ctx {
    uint32_t state[4];
    uint32_t pbytes;
    uint32_t rbytes;
    uint8_t *buffer;
} XXHASH32_CTX;

typedef struct xxhash64_ctx {
    uint64_t state[4];
    uint32_t pbytes;
    uint32_t rbytes;
    uint8_t *buffer;
} XXHASH64_CTX;


extern void
xxhash32_init(XXHASH32_CTX *ctx,
              uint32_t seed);

extern void
xxhash64_init(XXHASH64_CTX *ctx,
              uint64_t seed);

extern int
xxhash32_update(XXHASH32_CTX *ctx,
                const void *buf,
                size_t nbytes);

extern int
xxhash64_update(XXHASH64_CTX *ctx,
                const void *buf,
                size_t nbytes);

extern size_t
xxhash32_final(XXHASH32_CTX *ctx,
               void *rbuf,
               size_t rbufsize);

extern size_t
xxhash64_final(XXHASH64_CTX *ctx,
               void *rbuf,
               size_t rbufsize);

#endif
