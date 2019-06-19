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

#include "util/attribs.h"

typedef struct AImage AImage;
typedef struct AImageReader AImageReader;
typedef struct RuRend RuRend;

typedef enum RuRendUseExternalFormat {
    RU_REND_USE_EXTERNAL_FORMAT_AUTO = 0,
    RU_REND_USE_EXTERNAL_FORMAT_ALWAYS,
    RU_REND_USE_EXTERNAL_FORMAT_NEVER,
} RuRendUseExternalFormat;

struct ru_rend_new_args {
    bool use_validation;
    RuRendUseExternalFormat use_external_format;
};

#define ru_rend_new(...) ru_rend_new_s((struct ru_rend_new_args) { 0, __VA_ARGS__ })
RuRend *ru_rend_new_s(struct ru_rend_new_args args) _must_use_result_;
void ru_rend_free(RuRend *r);

void ru_rend_bind_window(RuRend *r, ANativeWindow *window);
void ru_rend_unbind_window(RuRend *r);

void ru_rend_start(RuRend *r, AImageReader *);
void ru_rend_stop(RuRend *r);
void ru_rend_pause(RuRend *r);
void ru_rend_unpause(RuRend *r);
