/*
 * fletcher.c
 *
 * This code is based on the code examples in Wikipedia:
 *  - https://en.wikipedia.org/wiki/Fletcher%27s_checksum
 *
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

#include "fletcher.h"

#include <stdio.h>
#include <stdint.h>


void
fletcher16_init(FLETCHER16_CTX *ctx) {
    ctx->c0 = 0;
    ctx->c1 = 0;
}

void
fletcher32_init(FLETCHER32_CTX *ctx) {
    ctx->c0 = 0;
    ctx->c1 = 0;
}

void
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
            ctx->c0 = ctx->c0 + *data++;
            ctx->c1 = ctx->c1 + ctx->c0;
        } while (--blocklen);
        ctx->c0 = ctx->c0 % 255;
        ctx->c1 = ctx->c1 % 255;
    }
}

void
fletcher32_update(FLETCHER32_CTX *ctx,
                  const uint16_t *data,
                  size_t len) {
    if (!data)
        return;
    
    len = (len + 1) & ~1;      /* Round up len to words */
 
    /* We similarly solve for n > 0 and n * (n+1) / 2 * (2^16-1) < (2^32-1) here. */
    /* On modern computers, using a 64-bit c0/c1 could allow a group size of 23726746. */
    while (len > 0) {
        size_t blocklen = len;
        
        if (blocklen > 360*2) {
            blocklen = 360*2;
        }
        len -= blocklen;
        do {
            ctx->c0 += *data++;
            ctx->c1 += ctx->c0;
        } while ((blocklen -= 2));
        ctx->c0 %= 65535;
        ctx->c1 %= 65535;
    }
}


uint16_t
fletcher16_final(FLETCHER16_CTX *ctx) {
    return (ctx->c1 << 8 | ctx->c0);
}

uint32_t
fletcher32_final(FLETCHER32_CTX *ctx) {
    return (ctx->c1 << 16 | ctx->c0);
}


