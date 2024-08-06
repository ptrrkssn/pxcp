/*
 * xxhash.c
 *
 * This xxhash code is written by Peter Eriksson, and is based of the C++ code
 * written by Stephan Brumme, which in turn based his work on the Yann Collet's
 * descriptions of XXHASH.
 *
 * For more information see:
 *  - http://create.stephan-brumme.com/disclaimer.html
 *  - http://cyan4973.github.io/xxHash/
 *
 *
 * Copyright (c) 2024, Peter Eriksson <pen@lysator.liu.se>
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

#include "xxhash.h"

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

static const uint32_t x32Prime1 = 2654435761U;
static const uint32_t x32Prime2 = 2246822519U;
static const uint32_t x32Prime3 = 3266489917U;
static const uint32_t x32Prime4 =  668265263U;
static const uint32_t x32Prime5 =  374761393U;

static const uint64_t x64Prime1 = 11400714785074694791ULL;
static const uint64_t x64Prime2 = 14029467366897019727ULL;
static const uint64_t x64Prime3 =  1609587929392839161ULL;
static const uint64_t x64Prime4 =  9650029242287828579ULL;
static const uint64_t x64Prime5 =  2870177450012600261ULL;


static inline uint32_t
rol32(uint32_t x,
      uint8_t bits) {
    return (x << bits) | (x >> (32 - bits));
}

static inline uint64_t
rol64(uint64_t x,
      uint8_t bits) {
    return (x << bits) | (x >> (64 - bits));
}

#define XXHASH32_PROCESS(block, state0, state1, state2, state3) \
    do { \
      state0 = rol32(state0 + *block++ * x32Prime2, 13) * x32Prime1; \
      state1 = rol32(state1 + *block++ * x32Prime2, 13) * x32Prime1; \
      state2 = rol32(state2 + *block++ * x32Prime2, 13) * x32Prime1; \
      state3 = rol32(state3 + *block++ * x32Prime2, 13) * x32Prime1; \
    } while(0)


static inline uint64_t x64_processOne(uint64_t prev,
                                         uint64_t input) {
    return rol64(prev + input * x64Prime2, 31) * x64Prime1;
}

#define XXHASH64_PROCESS(block, state0, state1, state2, state3) \
    do { \
        state0 = x64_processOne(state0, *block++);   \
        state1 = x64_processOne(state1, *block++);   \
        state2 = x64_processOne(state2, *block++);   \
        state3 = x64_processOne(state3, *block++);   \
    } while(0)



void
xxhash32_init(XXHASH32_CTX *ctx,
              uint32_t seed) {
    ctx->state[0] = seed+x32Prime1+x32Prime2;
    ctx->state[1] = seed+x32Prime2;
    ctx->state[2] = seed;
    ctx->state[3] = seed - x32Prime1;
    ctx->pbytes = 0;
    ctx->rbytes = 0;
    ctx->buffer = NULL;
}

void
xxhash64_init(XXHASH64_CTX *ctx,
              uint64_t seed) {
    ctx->state[0] = seed+x64Prime1+x64Prime2;
    ctx->state[1] = seed+x64Prime2;
    ctx->state[2] = seed;
    ctx->state[3] = seed - x64Prime1;
    ctx->pbytes = 0;
    ctx->rbytes = 0;
    ctx->buffer = NULL;
}


int
xxhash32_update(XXHASH32_CTX *ctx,
                const void *bufp,
                size_t nbytes) {
    uint32_t s0, s1, s2, s3;
    const uint32_t *block;
    size_t dsize, rsize;
    const uint8_t *buf = (const uint8_t *) bufp;

    
    /* Sanity checks */
    if (!buf || !nbytes) {
        errno = EINVAL;
        return -1;
    }

    /* An odd size buf was last used, this is only OK for final block */
    if (ctx->rbytes) {
        errno = EINVAL;
        return -1;
    }
        
    s0 = ctx->state[0];
    s1 = ctx->state[1];
    s2 = ctx->state[2];
    s3 = ctx->state[3];


    dsize = nbytes/16;
    rsize = nbytes%16;

    block = (uint32_t *) buf;
    while (dsize-- > 0) {
        XXHASH32_PROCESS(block, s0, s1, s2, s3);
    }

    ctx->state[0] = s0;
    ctx->state[1] = s1;
    ctx->state[2] = s2;
    ctx->state[3] = s3;

    ctx->pbytes += nbytes;
    ctx->rbytes = rsize;
    ctx->buffer = (uint8_t *) block;

    return 0;
}


int
xxhash64_update(XXHASH64_CTX *ctx,
                const void *bufp,
                size_t nbytes) {
    uint64_t s0, s1, s2, s3;
    const uint64_t *block;
    size_t dsize, rsize;
    const uint8_t *buf = (const uint8_t *) bufp;

    
    /* Sanity checks */
    if (!buf || !nbytes) {
        errno = EINVAL;
        return -1;
    }

    /* An odd size buf was last used, this is only OK for final block */
    if (ctx->rbytes) {
        errno = EINVAL;
        return -1;
    }
        
    s0 = ctx->state[0];
    s1 = ctx->state[1];
    s2 = ctx->state[2];
    s3 = ctx->state[3];


    dsize = nbytes/32;
    rsize = nbytes%32;

    block = (uint64_t *) buf;
    while (dsize-- > 0) {
        XXHASH64_PROCESS(block, s0, s1, s2, s3);
    }

    ctx->state[0] = s0;
    ctx->state[1] = s1;
    ctx->state[2] = s2;
    ctx->state[3] = s3;

    ctx->pbytes += nbytes;
    ctx->rbytes = rsize;
    ctx->buffer = (uint8_t *) block;

    return 0;
}


size_t
xxhash32_final(XXHASH32_CTX *ctx,
               void *rbuf,
               size_t rbufsize) {
    uint32_t *rp = (uint32_t *) rbuf;
    uint32_t result = ctx->pbytes;

    if (rbufsize < sizeof(result)) {
        errno = EINVAL;
        return -1;
    }

    if (ctx->pbytes > 16)
        result +=
            rol32(ctx->state[0],  1) +
            rol32(ctx->state[1],  7) +
            rol32(ctx->state[2], 12) +
            rol32(ctx->state[3], 18);
    else
        result += ctx->state[2] + x32Prime5;

    if (ctx->rbytes > 0) {
        uint32_t dsize = ctx->rbytes/4;
        uint32_t rsize = ctx->rbytes%4;

        uint32_t *block = (uint32_t *) ctx->buffer;
        uint8_t *buf;

        while (dsize-- > 0)
            result = rol32(result + *block++ * x32Prime3, 17) * x32Prime4;

        buf = (uint8_t *) block;
        while (rsize-- > 0)
            result = rol32(result + *buf++ * x32Prime5, 11) * x32Prime1;
    }

    result ^= result >> 15;
    result *= x32Prime2;
    result ^= result >> 13;
    result *= x32Prime3;
    result ^= result >> 16;

    *rp = result;
    return sizeof(result);
}


size_t
xxhash64_final(XXHASH64_CTX *ctx,
               void *rbuf,
               size_t rbufsize) {
    uint64_t *rp = (uint64_t *) rbuf;
    uint64_t result;

    if (rbufsize < sizeof(result)) {
        errno = EINVAL;
        return -1;
    }

    if (ctx->pbytes > 32) {
        result =
            rol64(ctx->state[0],  1) +
            rol64(ctx->state[1],  7) +
            rol64(ctx->state[2], 12) +
            rol64(ctx->state[3], 18);

        result = (result ^ x64_processOne(0, ctx->state[0])) * x64Prime1 + x64Prime4;
        result = (result ^ x64_processOne(0, ctx->state[1])) * x64Prime1 + x64Prime4;
        result = (result ^ x64_processOne(0, ctx->state[2])) * x64Prime1 + x64Prime4;
        result = (result ^ x64_processOne(0, ctx->state[3])) * x64Prime1 + x64Prime4;
    }
    else
        result = ctx->state[2] + x64Prime5;

    result += ctx->pbytes;
    
    if (ctx->rbytes > 0) {
        uint32_t dsize = ctx->rbytes/8;
        uint32_t rsize = ctx->rbytes%8;
 
        uint64_t *block = (uint64_t *) ctx->buffer;

        while (dsize-- > 0)
            result = rol64(result ^ x64_processOne(0, *block++), 27) * x64Prime1 + x64Prime4;

        if (rsize > 0) {
            uint32_t *data = (uint32_t *) block;
            uint8_t *buf;
            
            dsize = rsize/4;
            rsize = rsize%4;
            
            while (dsize-- > 0)
                result = rol64(result ^ (*data++) * x64Prime1, 23) * x64Prime2 + x64Prime3;
            
            buf = (uint8_t *) data;
            while (rsize-- > 0) {
                result = rol64(result ^ (*buf++) * x64Prime5, 11) * x64Prime1;
            }
        }
    }

    result ^= result >> 33;
    result *= x64Prime2;
    result ^= result >> 29;
    result *= x64Prime3;
    result ^= result >> 32;

    *rp = result;
    return sizeof(result);
}

