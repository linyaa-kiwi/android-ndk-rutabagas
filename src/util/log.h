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

#ifdef ANDROID
#include <android/log.h>
#else
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ANDROID
#define logv(fmt, ...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, (fmt), ##__VA_ARGS__)
#define logd(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, (fmt), ##__VA_ARGS__)
#define logi(fmt, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, (fmt), ##__VA_ARGS__)
#define logw(fmt, ...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, (fmt), ##__VA_ARGS__)
#define loge(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, (fmt), ##__VA_ARGS__)
#define logf(fmt, ...) __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, (fmt), ##__VA_ARGS__)
#define log_assert(cond, fmt, ...) __android_log_assert((cond), LOG_TAG, (fmt), ##__VA_ARGS__)
#define log_loc_assert(cond, fmt, ...) log_assert((cond), "%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define logv(fmt, ...) printf((fmt), ##__VA_ARGS__)
#define logd(fmt, ...) printf((fmt), ##__VA_ARGS__)
#define logi(fmt, ...) printf((fmt), ##__VA_ARGS__)
#define logw(fmt, ...) printf((fmt), ##__VA_ARGS__)
#define loge(fmt, ...) printf((fmt), ##__VA_ARGS__)
#define logf(fmt, ...) printf((fmt), ##__VA_ARGS__)
#endif

#define logv_loc(fmt, ...) logv("%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define logd_loc(fmt, ...) logd("%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define logi_loc(fmt, ...) logi("%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define logw_loc(fmt, ...) logw("%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define loge_loc(fmt, ...) loge("%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define logf_loc(fmt, ...) logf("%s:%d: %s: " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
