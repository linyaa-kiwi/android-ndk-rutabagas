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
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Linux
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Workaround a bug in NdkMediaCodec.h, which contains invalid C.
typedef struct AMediaCodecOnAsyncNotifyCallback AMediaCodecOnAsyncNotifyCallback;

// Android
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaError.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

// local
#include "util/alloc.h"
#include "util/check.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/ru_chan.h"

#include "ru_media.h"
#include <android/asset_manager.h>


#define RU_MEDIA_MAX_IMAGE_COUNT 8

typedef struct RuMediaEvent {
    enum {
        RU_MEDIA_EVENT_START,
        RU_MEDIA_EVENT_STOP,
        RU_MEDIA_EVENT_BUFFER_IN,
        RU_MEDIA_EVENT_BUFFER_OUT,
    } type;

    union {
        struct {
            uint32_t index;
        } buffer_in;

        struct {
            uint32_t index;
            AMediaCodecBufferInfo info;
        } buffer_out;
    };
} RuMediaEvent;

typedef struct RuMedia {
    pthread_t thread; // See ru_media_thread().

    uint32_t track; // We play a single track, the first video track.
    AMediaFormat *format;
    AMediaExtractor *ex;
    AMediaCodec *codec;
    AImageReader *image_reader;

    // RuMedia::codec feeds the channel through AMediaCodecOnAsyncNotifyCallback.
    // RuMedia::thread drains the channel and forwards each index to
    // AMediaCodec_queueInputBuffer or AMediaCodec_releaseOutputBuffer.
    RuChan event_chan;

    AAsset *asset;

} RuMedia;

static void
ru_media_push_event(RuMedia *m, RuMediaEvent ev) {
    ru_chan_push(&m->event_chan, &ev);
}

static void *
ru_media_thread(void *_media) {
    logd("media: start thread tid=%d", gettid());

    RuMedia *m = _media;
    int ret;

    for (;;) {
        RuMediaEvent ev;
        ru_chan_pop_wait(&m->event_chan, &ev);

        switch (ev.type) {
            case RU_MEDIA_EVENT_START:
                logd("media: pop_MEDIA_EVENT_START");
                ret = AMediaCodec_start(m->codec);
                if (ret)
                    die("media: AMediaCodec_start failed: error=%d", ret);
                break;
            case RU_MEDIA_EVENT_STOP:
                logd("media: pop_MEDIA_EVENT_STOP");
                goto done;
            case RU_MEDIA_EVENT_BUFFER_IN: {
                int index = ev.buffer_in.index;
                logd("media: pop_MEDIA_EVENT_BUFFER_IN(index=%d)", index);

                size_t buf_size;
                uint8_t *buf = AMediaCodec_getInputBuffer(m->codec, index, &buf_size);
                logd("media: buf=%p buf_size=%zu", buf, buf_size);
                if (!buf) {
                    die("media: AMediaCodec_getInputBuffer(index=%d) failed", index);
                }

                ssize_t sample_size = AMediaExtractor_readSampleData(m->ex, buf, buf_size);
                logd("media: sample size: %zd", sample_size);

                int64_t sample_time = AMediaExtractor_getSampleTime(m->ex);
                logd("media: sample time: %"PRIi64, sample_time);

                bool eos = sample_size < 0 || !AMediaExtractor_advance(m->ex);
                if (eos)
                    logd("media: end of input stream");

                uint32_t flags = 0;
                if (eos)
                    flags |= AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;

                if (sample_size < 0) {
                    // AMediaCodec_queueInputBuffer will fail if given negative
                    // sample size.
                    sample_size = 0;
                }

                ret = AMediaCodec_queueInputBuffer(m->codec, index,
                        /*offset*/ 0, sample_size, sample_time, flags);
                if (ret) {
                    die("media: AMediaCodec_queueInputBuffer(index=%d) "
                            "failed: error=%d", index, ret);
                }
                break;
            }
            case RU_MEDIA_EVENT_BUFFER_OUT: {
                int index = ev.buffer_out.index;
                logd("media: pop_MEDIA_EVENT_BUFFER_OUT(index=%d)", index);

                bool eos = (ev.buffer_out.info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                if (eos)
                    logd("media: end of output stream");

                bool render = (ev.buffer_out.info.size > 0);

                ret = AMediaCodec_releaseOutputBuffer(m->codec, ev.buffer_out.index, render);
                if (ret) {
                    die("media: AMediaCodec_releaseOutputBuffer(index=%d) "
                            "failed: error=%d", ev.buffer_out.index, ret);
                }

                if (eos)
                    goto done;
            }
        }
    }

 done:
    AMediaCodec_stop(m->codec);
    return NULL;
}

static void
on_codec_error(AMediaCodec *codec, void *_media, media_status_t error,
               int32_t action, const char *detail) {
    logd("media: %s: error=%d action=%d: %s", __func__, error, action, detail);
    logd("media: %s: FINISHME", __func__);
}

static void
on_codec_format_changed(AMediaCodec *codec, void *_media, AMediaFormat *format) {
    logd("media: %s: %s", __func__, AMediaFormat_toString(format));
    logd("media: %s: FINISHME", __func__);
}

static void
on_codec_input_available(AMediaCodec *codec, void *_media, int32_t index) {
    RuMedia *m = _media;

    logd("media: push RU_MEDIA_EVENT_BUFFER_IN(index=%d)", index);
    ru_media_push_event(m,
        (RuMediaEvent) {
            .type = RU_MEDIA_EVENT_BUFFER_IN,
            .buffer_in = {
                .index = index,
            },
        });
}

static void
on_codec_output_available(AMediaCodec *codec, void *_media, int32_t index,
        AMediaCodecBufferInfo *info) {
    RuMedia *m = _media;

    logd("media: push RU_MEDIA_EVENT_BUFFER_OUT(index=%d)", index);
    ru_media_push_event(m,
        (RuMediaEvent) {
            .type = RU_MEDIA_EVENT_BUFFER_OUT,
            .buffer_out = {
                .index = index,
                .info = *info,
            },
        });
}

static void
select_track(AMediaExtractor *ex, uint32_t *out_track,
        AMediaFormat **out_format)
{
    int ret;

    uint32_t n_tracks = AMediaExtractor_getTrackCount(ex);
    logd("media: file has %u tracks", n_tracks);
    logd("media: search for first video track");

    uint32_t track = 0;
    AMediaFormat *format = NULL;
    for (; track < n_tracks; ++track) {
        logd("media: inspect track %u", track);

        format = AMediaExtractor_getTrackFormat(ex, track);
        if (!format)
            die("media: AMediaExtractor_getTrackFormat(%u) failed", track);

        logd("media: track: %u, %s", track, AMediaFormat_toString(format));

        const char *mime = NULL; // owned by format
        if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime))
            die("media: track: %u, AMediaFormat_getString(AMEDIAFORMAT_KEY_MIME) failed", track);

        logd("media: track: %u, mime: %s", track, mime);

        if (strncmp(mime, "video/", 6) == 0)
            break;

        logd("media: ignore track %u", track);
        AMediaFormat_delete(format);
    }

    if (track == n_tracks)
        die("media: failed to find video track");

    logd("media: select track %u", track);
    ret = AMediaExtractor_selectTrack(ex, track);
    if (ret)
        die("media: AMediaExtractor_selectTrack(%u) failed", track);

    *out_track = track;
    *out_format = format;
}

RuMedia *
ru_media_new(const char *src_path) {
    int ret;

    let m = new0(RuMedia);

    ru_chan_init(&m->event_chan, sizeof(RuMediaEvent), 64);

    logd("media: open file: %s", src_path);
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1)
        die("media: failed to open file: %s", src_path);

    off_t src_len = lseek(src_fd, 0, SEEK_END);
    if (src_len == -1)
        die("media: failed to query size of file");

    m->ex = AMediaExtractor_new();
    if (!m->ex)
        abort();

    ret = AMediaExtractor_setDataSourceFd(m->ex, src_fd, /*offset*/ 0, src_len);
    if (ret)
        die("media: AMediaExtractor_setDataSourceFd failed: error=%d", ret);

    close(src_fd);

    select_track(m->ex, &m->track, &m->format);

    int32_t width, height;
    if (!AMediaFormat_getInt32(m->format, AMEDIAFORMAT_KEY_WIDTH, &width) ||
        !AMediaFormat_getInt32(m->format, AMEDIAFORMAT_KEY_HEIGHT, &height)) {
        die("media: failed to query AMediaFormat width, height");
    }

    ret = AImageReader_newWithUsage(
        width, height,
        AIMAGE_FORMAT_YUV_420_888,
        AHARDWAREBUFFER_USAGE_CPU_READ_NEVER |
        AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER |
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
        RU_MEDIA_MAX_IMAGE_COUNT,
        &m->image_reader);
    if (ret)
        die("media: AImageReader_newWithUsage failed: error=%d", ret);

    ANativeWindow *surface;
    ret = AImageReader_getWindow(m->image_reader, &surface);
    if (ret)
        die("media: AImageReader_getWindow failed: error=%d", ret);

    const char *mime = NULL; // owned by format
    if (!AMediaFormat_getString(m->format, AMEDIAFORMAT_KEY_MIME, &mime))
        die("media: track: %u, AMediaFormat_getString(AMEDIAFORMAT_KEY_MIME) failed", m->track);

    m->codec = AMediaCodec_createDecoderByType(mime);
    if (!m->codec)
        die("media: AMediaCodec_createDeocderByType(%s) failed", mime);

    ret = AMediaCodec_configure(m->codec, m->format, surface, /*crypto*/ NULL,
            /*flags*/ 0);
    if (ret)
        die("media: AMediaCodec_configure failed: error=%d", ret);

    AMediaCodecOnAsyncNotifyCallback codec_notify_cb = {
        .onAsyncError = on_codec_error,
        .onAsyncFormatChanged = on_codec_format_changed,
        .onAsyncInputAvailable = on_codec_input_available,
        .onAsyncOutputAvailable = on_codec_output_available,
    };

    ret = AMediaCodec_setAsyncNotifyCallback(m->codec, codec_notify_cb, m);
    if (ret)
        die("media: AMediaCodec_setAsyncNotifyCallback failed: error=%d", ret);

    if (pthread_create(&m->thread, NULL, ru_media_thread, m))
        abort();

    return m;
}

RuMedia *
ru_asset_media_new(AAssetManager *asset_mgr, const char *src_path) {
    int ret;

    let m = new0(RuMedia);

    ru_chan_init(&m->event_chan, sizeof(RuMediaEvent), 64);

    logd("media: open file: %s", src_path);
    m->asset = AAssetManager_open(asset_mgr, src_path, 0);
    if(!m->asset) die("media: failed to open media in asset");
    off_t start, len;
    int src_fd = AAsset_openFileDescriptor(m->asset, &start, &len);
    if (src_fd == -1)
        die("media: failed to open file: %s", src_path);

    if (len == -1)
        die("media: failed to query size of file");

    m->ex = AMediaExtractor_new();
    if (!m->ex)
        abort();

    ret = AMediaExtractor_setDataSourceFd(m->ex, src_fd, start, len);
    if (ret)
        die("media: AMediaExtractor_setDataSourceFd failed: error=%d", ret);

    select_track(m->ex, &m->track, &m->format);

    int32_t width, height;
    if (!AMediaFormat_getInt32(m->format, AMEDIAFORMAT_KEY_WIDTH, &width) ||
        !AMediaFormat_getInt32(m->format, AMEDIAFORMAT_KEY_HEIGHT, &height)) {
        die("media: failed to query AMediaFormat width, height");
    }

    ret = AImageReader_newWithUsage(
            width, height,
            AIMAGE_FORMAT_YUV_420_888,
            AHARDWAREBUFFER_USAGE_CPU_READ_NEVER |
            AHARDWAREBUFFER_USAGE_CPU_WRITE_NEVER |
            AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
            RU_MEDIA_MAX_IMAGE_COUNT,
            &m->image_reader);
    if (ret)
        die("media: AImageReader_newWithUsage failed: error=%d", ret);

    ANativeWindow *surface;
    ret = AImageReader_getWindow(m->image_reader, &surface);
    if (ret)
        die("media: AImageReader_getWindow failed: error=%d", ret);

    const char *mime = NULL; // owned by format
    if (!AMediaFormat_getString(m->format, AMEDIAFORMAT_KEY_MIME, &mime))
        die("media: track: %u, AMediaFormat_getString(AMEDIAFORMAT_KEY_MIME) failed", m->track);

    m->codec = AMediaCodec_createDecoderByType(mime);
    if (!m->codec)
        die("media: AMediaCodec_createDeocderByType(%s) failed", mime);

    ret = AMediaCodec_configure(m->codec, m->format, surface, /*crypto*/ NULL,
            /*flags*/ 0);
    if (ret)
        die("media: AMediaCodec_configure failed: error=%d", ret);

    AMediaCodecOnAsyncNotifyCallback codec_notify_cb = {
            .onAsyncError = on_codec_error,
            .onAsyncFormatChanged = on_codec_format_changed,
            .onAsyncInputAvailable = on_codec_input_available,
            .onAsyncOutputAvailable = on_codec_output_available,
    };

    ret = AMediaCodec_setAsyncNotifyCallback(m->codec, codec_notify_cb, m);
    if (ret)
        die("media: AMediaCodec_setAsyncNotifyCallback failed: error=%d", ret);

    if (pthread_create(&m->thread, NULL, ru_media_thread, m))
        abort();

    return m;
}

void
ru_media_free(RuMedia *m) {
    if (!m)
        return;

    ru_media_stop(m);

    if (pthread_join(m->thread, NULL))
        abort();

    AImageReader_delete(m->image_reader);
    AMediaCodec_delete(m->codec);
    AMediaExtractor_delete(m->ex);
    AMediaFormat_delete(m->format);
    ru_chan_finish(&m->event_chan);
    AAsset_close(m->asset);
    free(m);
}

void
ru_media_start(RuMedia *m) {
    logd("media: push RU_MEDIA_EVENT_START");
    ru_media_push_event(m,
        (RuMediaEvent) {
            .type = RU_MEDIA_EVENT_START,
        });
}

void
ru_media_stop(RuMedia *m) {
    logd("media: push RU_MEDIA_EVENT_STOP");
    ru_media_push_event(m,
        (RuMediaEvent) {
            .type = RU_MEDIA_EVENT_STOP,
        });
}

AImageReader *
ru_media_get_aimage_reader(RuMedia *m) {
    assert(m->image_reader);
    return m->image_reader;
}
