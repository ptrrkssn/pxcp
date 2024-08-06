/*
** fletcher.h - FLETCHER Digest/Checksum functions
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

#ifndef PXCP_FLETCHER_H
#define PXCP_FLETCHER_H

#include <stdint.h>
#include <sys/types.h>

typedef struct fletcher16_ctx {
    uint32_t c0;
    uint32_t c1;
} FLETCHER16_CTX;

typedef struct fletcher32_ctx {
    uint32_t c0;
    uint32_t c1;
} FLETCHER32_CTX;

extern void
fletcher16_init(FLETCHER16_CTX *ctx);

extern void
fletcher32_init(FLETCHER32_CTX *ctx);

extern void
fletcher16_update(FLETCHER16_CTX *ctx,
                  const uint8_t *data,
                  size_t len);

extern void
fletcher32_update(FLETCHER32_CTX *ctx,
                  const uint16_t *data,
                  size_t len);

extern uint16_t
fletcher16_final(FLETCHER16_CTX *ctx);

extern uint32_t
fletcher32_final(FLETCHER32_CTX *ctx);

#endif
