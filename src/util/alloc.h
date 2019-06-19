// Copyright 2019 Google Inc
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of Google Inc. nor the names of its contributors may be
//    used to endorse or promote products derived from this software without
//    specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <stddef.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

#include "attribs.h"
#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void
freep(void *pp) {
    free(*(void**) pp);
}

#define _cleanup_free_ _cleanup_(freep)

noreturn void oom(void);
void *memset(void *p, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

_returns_nonnull_ _must_use_result_
static inline void *
check_alloc(void *p) {
    if (!p)
        oom();
    return p;
}

static inline size_t
check_alloc_mul(size_t m, size_t n) {
    size_t size;

    if (__builtin_mul_overflow(m, n, &size))
        oom();

    return size;
}

_returns_nonnull_ _must_use_result_ _malloc_ _alloc_(1) _flatten_
static inline void *
xmalloc(size_t size) {
    return check_alloc(malloc(size));
}

_returns_nonnull_ _must_use_result_ _malloc_ _alloc_(1) _flatten_
static inline void *
xmalloc0(size_t size) {
    return check_alloc(calloc(1, size));
}

_returns_nonnull_ _must_use_result_ _malloc_ _alloc_(1, 2) _flatten_
static inline void *
xmallocn(size_t m, size_t n) {
    let size = check_alloc_mul(m, n);
    return xmalloc(size);
}

_returns_nonnull_ _must_use_result_ _malloc_ _alloc_(1, 2) _flatten_
static inline void *
xmallocn0(size_t m, size_t n) {
    return check_alloc(calloc(m, n));
}

_returns_nonnull_ _must_use_result_ _alloc_(2) _flatten_
static inline void *
xrealloc(void *p, size_t size) {
    return check_alloc(realloc(p, size));
}

_returns_nonnull_ _must_use_result_ _alloc_(2, 3) _flatten_
static inline void *
xreallocn(void *p, size_t m, size_t n) {
    let size = check_alloc_mul(m, n);
    return xrealloc(p, size);
}

_returns_nonnull_ _must_use_result_ _alloc_(2) _flatten_
static inline void *
xmemdup(const void *p, size_t n) {
    return memcpy(xmalloc(n), p, n);
}

_returns_nonnull_ _must_use_result_ _alloc_(2, 3) _flatten_
static inline void *
xmemdupn(const void *p, size_t m, size_t n) {
    let size = check_alloc_mul(m, n);
    return xmemdup(p, size);
}

_returns_nonnull_ _must_use_result_ _malloc_ _flatten_
static inline char *
xstrdup(const char *s) {
   return check_alloc(strdup(s));
}

_returns_nonnull_ _must_use_result_ _malloc_ _flatten_
static inline char *
xstrndup(const char *s, size_t n) {
   return check_alloc(strndup(s, n));
}

#define memzero(ptr, n) ((void) memset((ptr), 0, (n)))
#define zero(x) memzero(&(x), sizeof(x))

#ifndef __cplusplus
#define new(T) ((T*) xmalloc(sizeof(T)))
#define new0(T) ((T*) xmalloc0(sizeof(T)))
#define new_array(T, n) ((T*) xmallocn((n), sizeof(T)))
#define new0_array(T, n) ((T*) xmallocn0((n), sizeof(T)))

#define new_init(T, ...) \
    __new_init(UNIQ(ptr), T, ##__VA_ARGS__)
#define __new_init(uniq_ptr, T, ...) ({ \
        let uniq_ptr = new(T); \
        *uniq_ptr = (T) { __VA_ARGS__ }; \
        uniq_ptr; \
})

#endif // __cplusplus

#ifdef __cplusplus
}
#endif
