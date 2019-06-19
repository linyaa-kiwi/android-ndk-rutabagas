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

// Function Attributes {{{
#define _const_ __attribute__((__const__))
#define _pure_ __attribute__((__pure__))
#define _must_use_result_ __attribute__((__warn_unused_result__))
#define _returns_nonnull_ __attribute__((__returns_nonnull__))
#define _printf_(a, b) __attribute__((__format__(__printf__, (a), (b))))
#define _malloc_ __attribute__((__malloc__))

#ifdef HAVE_C_ATTRIBUTE_ALLOC_SIZE
#define _alloc_(...) __attribute__((__alloc_size__(__VA_ARGS__)))
#else
#define _alloc_(...)
#endif

#define _flatten_ __attribute__((__flatten__))
// }}}

// Type Attributes {{{
#define _atomic_ _Atomic
#define _packed_ __attribute__((__packed__))
// }}}

// Common Attributes {{{
#define _used_ __attribute__((__used__))
#define _unused_ __attribute__((__unused__))
// }}}

// Variable Attributes {{{
#define _cleanup_(f) __attribute__((__cleanup__(f)))

// Empty documentation attributes.
#define _owned_by_(x)
#define _not_owned_
// }}}
