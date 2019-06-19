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

// stdlib
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Android
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>

// local
#include "util/alloc.h"
#include "util/attribs.h"
#include "util/check.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/ru_ndk.h"

#include "ru_app.h"
#include "ru_media.h"
#include "ru_rend.h"

typedef struct RuApp {
    struct android_app *android;
    RuMedia *media;
    RuRend *rend;
} RuApp;

static void on_app_cmd(struct android_app *android, int32_t cmd);

static char *
get_arg(struct android_app *android, const char *name) {
    char *s = ru_activity_get_string_extra(android->activity, name);
    logd("arg: %s=\"%s\"", name, s);
    return s;
}

RuApp *
ru_app_new(struct android_app *android) {
    _cleanup_free_ char *media_src = get_arg(android, "mediaSrc");
    if (!media_src)
        die("cmdline missing `-e mediaSrc <path>`");

    RuRendUseExternalFormat use_ext_format = RU_REND_USE_EXTERNAL_FORMAT_AUTO;
    _cleanup_free_ char *use_ext_format_s = get_arg(android, "useVkExternalFormat");

    if (!use_ext_format_s) {
        // default
    } else if (!strcmp(use_ext_format_s, "auto")) {
        use_ext_format = RU_REND_USE_EXTERNAL_FORMAT_AUTO;
    } else if (!strcmp(use_ext_format_s, "always")) {
        use_ext_format = RU_REND_USE_EXTERNAL_FORMAT_ALWAYS;
    } else if (!strcmp(use_ext_format_s, "never")) {
        use_ext_format = RU_REND_USE_EXTERNAL_FORMAT_NEVER;
    } else {
        die("bad value for useVkExternalFormat: %s", use_ext_format_s);
    }

    bool use_validation = false;
    _cleanup_free_ char *use_validation_s = get_arg(android, "useVkValidation");

    if (!use_validation_s) {
        // default
    } else if (!strcmp(use_validation_s, "false")) {
        use_validation = false;
    } else if (!strcmp(use_validation_s, "true")) {
        use_validation = true;
    } else {
        die("bad value for useVkValidation: %s", use_validation_s);
    }

    let app = new0(RuApp);
    app->android = android;
    app->android->userData = app;
    app->android->onAppCmd = on_app_cmd;
    app->media = ru_media_new(media_src);
    app->rend = ru_rend_new(
        .use_validation = use_validation,
        .use_external_format = use_ext_format);

    return app;
}

void
ru_app_free(RuApp *app) {
    if (!app)
        return;
    ru_rend_free(app->rend);
    ru_media_free(app->media);
    free(app);
}

void
ru_app_loop(RuApp *app) {
    for (;;) {
        int looper_id;
        int events;
        void *event_data = NULL;
        int32_t timeout_ms = -1;

        looper_id = ALooper_pollAll(timeout_ms, /*outFd*/ NULL, &events,
                    &event_data);

        switch (looper_id) {
            case ALOOPER_POLL_ERROR:
                loge("ALooper_pollAll failed");
                break;
            case LOOPER_ID_MAIN:
            case LOOPER_ID_INPUT: {
                struct android_poll_source *event_src = event_data;
                event_src->process(app->android, event_src);
                break;
            }
            default:
                // Catches timeouts, spurious wakeups, etc.
                // See <android/looper.h>.
                break;
        }

    }
}

static const char *
app_cmd_to_str(int32_t cmd) {
    #define CASE(cmd) case cmd: return #cmd

    switch (cmd) {
        CASE(APP_CMD_START);
        CASE(APP_CMD_INIT_WINDOW);
        CASE(APP_CMD_TERM_WINDOW);
        CASE(APP_CMD_GAINED_FOCUS);
        CASE(APP_CMD_RESUME);
        CASE(APP_CMD_LOST_FOCUS);
        CASE(APP_CMD_PAUSE);
        CASE(APP_CMD_STOP);
        CASE(APP_CMD_SAVE_STATE);
        CASE(APP_CMD_INPUT_CHANGED);
        CASE(APP_CMD_WINDOW_RESIZED);
        CASE(APP_CMD_WINDOW_REDRAW_NEEDED);
        CASE(APP_CMD_CONTENT_RECT_CHANGED);
        CASE(APP_CMD_CONFIG_CHANGED);
        CASE(APP_CMD_LOW_MEMORY);
        CASE(APP_CMD_DESTROY);
        default:
            die("unknown APP_CMD %d", cmd);
    }

    #undef CASE
}

static void
on_app_cmd(struct android_app *android, int32_t cmd) {
    RuApp *app = android->userData;

    logd("consume %s", app_cmd_to_str(cmd));

    // See <https://developer.android.com/guide/components/activities/activity-lifecycle.html>
    switch (cmd) {
        case APP_CMD_START:
            ru_rend_start(app->rend, ru_media_get_aimage_reader(app->media));
            ru_media_start(app->media);
            break;
        case APP_CMD_INIT_WINDOW:
            ru_rend_bind_window(app->rend, android->window);
            break;
        case APP_CMD_TERM_WINDOW:
            ru_rend_unbind_window(app->rend);
            break;
        case APP_CMD_GAINED_FOCUS:
            ru_rend_unpause(app->rend);
            break;
        case APP_CMD_LOST_FOCUS:
            ru_rend_pause(app->rend);
            break;
        case APP_CMD_STOP:
        case APP_CMD_RESUME:
        case APP_CMD_PAUSE:
        case APP_CMD_SAVE_STATE:
        case APP_CMD_INPUT_CHANGED:
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_WINDOW_REDRAW_NEEDED:
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_LOW_MEMORY:
            // FINISHME; Handle more APP_CMD_*
            break;
        case APP_CMD_DESTROY:
            return;
        default:
            die("unknown APP_CMD %d", cmd);
    }
}

void
android_main(struct android_app *android) {
    RuApp *app = ru_app_new(android);
    ru_app_loop(app);
    ru_app_free(app);
}
