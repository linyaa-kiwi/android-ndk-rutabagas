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

#define CONCAT(x, y) CONCAT2(x, y)
#define CONCAT2(x, y) x##y

// Generate a unique token in the current translation unit.
#define UNIQ(prefix) CONCAT(prefix##_uniq, __COUNTER__)

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#define typeof(x) __typeof__(x)
#define let __auto_type

// A quiet static_assert()
#define static_assert_q(cond) _Static_assert((cond), "")

#define for_range(i, begin, end) \
    __for_range(i, (begin), (end), UNIQ(_end))

#define __for_range(i, begin, end, uniq_end) \
    let uniq_end = (end); \
    for (let i = (begin); i < uniq_end; ++i)

static inline void ru_require_semicolon(void) {}
