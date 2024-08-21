/*
 * misc.c
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
#include "misc.h"
#include "digest.h"

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>



char *
strdupcat(const char *str,
	  ...) {
    va_list ap;
    char *retval, *res;
    const char *cp;
    size_t reslen;

    /* Get the length of the first string, plus 1 for ending NUL */
    reslen = strlen(str)+1;

    /* Add length of the other strings */
    va_start(ap, str);
    while ((cp = va_arg(ap, char *)) != NULL)
        reslen += strlen(cp);
    va_end(ap);

    /* Allocate storage */
    retval = res = malloc(reslen);
    if (!retval)
        return NULL;

    /* Get the first string */
    cp = str;
    while (*cp)
        *res++ = *cp++;

    /* And then append the rest */
    va_start(ap, str);
    while ((cp = va_arg(ap, char *)) != NULL) {
        while (*cp)
            *res++ = *cp++;
    }
    va_end(ap);

    /* NUL-terminate the string */
    *res = '\0';
    return retval;
}

int
get_int(char *start,
          char **next) {
    return strtol(start, next, 0);
}


int
get_digest(char *start,
           char **next) {
    int v;

    v = digest_str2type(start);
    if (v < 0)
        v = get_int(start, next);
    else {
	while (*start)
	    ++start;

        *next = start;
    }

    return v;
}
