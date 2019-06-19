// Copyright 2017 Chad Versace <chad@kiwitree.net>
// All rights reserved.
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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "attribs.h"
#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ru_max(a, b) __ru_max(UNIQ(_a), (a), UNIQ(_b), (b))

#define __ru_max(uniq_a, a, uniq_b, b) ({ \
    let uniq_a = (a); \
    let uniq_b = (b); \
    uniq_a >= uniq_b ? uniq_a : uniq_b; \
})

#define ru_min(a, b) __ru_min(UNIQ(_a), (a), UNIQ(_b), (b))

#define __ru_min(uniq_a, a, uniq_b, b) ({ \
    let uniq_a = (a); \
    let uniq_b = (b); \
    uniq_a <= uniq_b ? uniq_a : uniq_b; \
})

static inline _const_ bool
ru_is_pow2(uintmax_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

static inline _const_ bool
ru_is_aligned(uintmax_t n, uintmax_t a) {
    assert(ru_is_pow2(a));
    return (n & (a - 1)) == 0;
}

// Return `n` aligned up to `a`, which must be a power of 2.
static inline _const_ uintmax_t
ru_align_umax(uintmax_t n, uintmax_t a) {
    assert(ru_is_pow2(a));
    return (n + (a - 1)) & ~(a - 1);
}

#ifdef __cplusplus
}
#endif
