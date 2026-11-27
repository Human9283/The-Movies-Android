/*
 * tm_asf_decoder.c - narrow ASF/WMV video decoder backed by FFmpeg.
 *
 * This module knows nothing about COM or guest memory. The DirectShow reader
 * will own one decoder and copy each returned RGB24 frame into an allocator
 * sample before calling the game's IMemInputPin::Receive.
 *
 * This is deliberately video-only. Audio streams are ignored for now.
 */
#include "tm_asf_decoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>

struct tm_asf_decoder {
    AVFormatContext *format;
    AVCodecContext *codec;
    AVPacket *packet;
    AVFrame *frame;
    struct SwsContext *scaler;
    uint8_t *rgb_top_down;
    size_t rgb_capacity;
    int video_stream;
    int width;
    int height;
    int64_t duration;
    AVRational time_base;
    AVRational frame_rate;
    int demux_eof;
    int flush_sent;
    int packet_pending;
    int failed;
};

static void set_error(char *out, size_t size, const char *operation, int code) {
    if (!out || !size)
        return;
    char detail[AV_ERROR_MAX_STRING_SIZE];
    if (code < 0)
        av_strerror(code, detail, sizeof(detail));
    else
        snprintf(detail, sizeof(detail), "%s", "invalid media");
    snprintf(out, size, "%s: %s (%d)", operation, detail, code);
}

static int valid_video_codec(enum AVCodecID id) {
    switch (id) {
        case AV_CODEC_ID_WMV1:
        case AV_CODEC_ID_WMV2:
        case AV_CODEC_ID_WMV3:
        case AV_CODEC_ID_VC1:
            return 1;
        default:
            return 0;
    }
}

static void fill_info(const tm_asf_decoder *d, tm_asf_video_info *info) {
    if (!info)
        return;
    memset(info, 0, sizeof(*info));
    info->width = d->width;
    info->height = d->height;
    info->codec_id = d->codec->codec_id;
    info->duration = d->duration;
    info->time_base_num = d->time_base.num;
    info->time_base_den = d->time_base.den;
    info->frame_rate_num = d->frame_rate.num;
    info->frame_rate_den = d->frame_rate.den;
    snprintf(info->codec_name, sizeof(info->codec_name), "%s",
             avcodec_get_name(d->codec->codec_id));
}

int tm_asf_decoder_open(
    const char *path,
    tm_asf_decoder **out,
    tm_asf_video_info *info,
    char *error_text,
    size_t error_text_size) {
    if (out)
        *out = NULL;
    if (!path || !path[0] || !out) {
        set_error(error_text, error_text_size, "tm_asf_decoder_open arguments", AVERROR(EINVAL));
        return AVERROR(EINVAL);
    }

    tm_asf_decoder *d = av_mallocz(sizeof(*d));
    if (!d) {
        set_error(error_text, error_text_size, "allocate decoder", AVERROR(ENOMEM));
        return AVERROR(ENOMEM);
    }
    d->video_stream = -1;

    int result = avformat_open_input(&d->format, path, NULL, NULL);
    if (result < 0) {
        set_error(error_text, error_text_size, "open input", result);
        goto fail;
    }
    if (!d->format->iformat || strcmp(d->format->iformat->name, "asf") != 0) {
        result = AVERROR_INVALIDDATA;
        set_error(error_text, error_text_size, "input is not ASF", result);
        goto fail;
    }

    result = avformat_find_stream_info(d->format, NULL);
    if (result < 0) {
        set_error(error_text, error_text_size, "read stream information", result);
        goto fail;
    }

    d->video_stream = av_find_best_stream(
        d->format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (d->video_stream < 0) {
        result = d->video_stream;
        set_error(error_text, error_text_size, "find video stream", result);
        goto fail;
    }

    AVStream *stream = d->format->streams[d->video_stream];
    if (!valid_video_codec(stream->codecpar->codec_id)) {
        result = AVERROR_DECODER_NOT_FOUND;
        set_error(error_text, error_text_size,
                  "video codec is outside the supported WMV/VC-1 set", result);
        goto fail;
    }

    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        result = AVERROR_DECODER_NOT_FOUND;
        set_error(error_text, error_text_size, "find video decoder", result);
        goto fail;
    }

    d->codec = avcodec_alloc_context3(decoder);
    if (!d->codec) {
        result = AVERROR(ENOMEM);
        set_error(error_text, error_text_size, "allocate codec context", result);
        goto fail;
    }

    result = avcodec_parameters_to_context(d->codec, stream->codecpar);
    if (result < 0) {
        set_error(error_text, error_text_size, "copy codec parameters", result);
        goto fail;
    }
    /* Make packet/frame timestamps explicitly use the demuxer's stream time base. */
    d->codec->pkt_timebase = stream->time_base;
    result = avcodec_open2(d->codec, decoder, NULL);
    if (result < 0) {
        set_error(error_text, error_text_size, "open video decoder", result);
        goto fail;
    }

    d->width = d->codec->width;
    d->height = d->codec->height;
    if (d->width <= 0 || d->height <= 0 || d->width > 4096 || d->height > 4096) {
        result = AVERROR_INVALIDDATA;
        set_error(error_text, error_text_size, "invalid video dimensions", result);
        goto fail;
    }

    d->time_base = stream->time_base;
    d->frame_rate = av_guess_frame_rate(d->format, stream, NULL);
    d->duration = stream->duration != AV_NOPTS_VALUE
        ? stream->duration
        : (d->format->duration != AV_NOPTS_VALUE
            ? av_rescale_q(d->format->duration, AV_TIME_BASE_Q, d->time_base)
            : AV_NOPTS_VALUE);

    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->packet || !d->frame) {
        result = AVERROR(ENOMEM);
        set_error(error_text, error_text_size, "allocate packet/frame", result);
        goto fail;
    }

    fill_info(d, info);
    *out = d;
    if (error_text && error_text_size)
        error_text[0] = '\0';
    return 0;

fail:
    tm_asf_decoder_close(d);
    return result;
}

static int receive_frame(
    tm_asf_decoder *d,
    uint8_t *dst,
    size_t dst_capacity,
    int dst_stride,
    tm_asf_frame_info *frame_info,
    char *error_text,
    size_t error_text_size) {
    int result = avcodec_receive_frame(d->codec, d->frame);
    if (result == AVERROR(EAGAIN))
        return 2;
    if (result == AVERROR_EOF)
        return TM_ASF_READ_EOF;
    if (result < 0) {
        set_error(error_text, error_text_size, "decode video frame", result);
        return TM_ASF_READ_ERROR;
    }

    if (d->frame->width != d->width || d->frame->height != d->height) {
        set_error(error_text, error_text_size,
                  "mid-stream video dimension change is unsupported", AVERROR_INVALIDDATA);
        av_frame_unref(d->frame);
        return TM_ASF_READ_ERROR;
    }

    const size_t row_bytes = (size_t)d->width * 3u;
    if (dst_stride < 0 || (size_t)dst_stride < row_bytes ||
        (size_t)dst_stride > SIZE_MAX / (size_t)d->height ||
        dst_capacity < (size_t)dst_stride * (size_t)d->height) {
        set_error(error_text, error_text_size, "RGB24 destination buffer is too small", AVERROR(ENOSPC));
        av_frame_unref(d->frame);
        return TM_ASF_READ_ERROR;
    }

    const size_t temp_size = row_bytes * (size_t)d->height;
    if (temp_size > d->rgb_capacity) {
        uint8_t *resized = av_realloc(d->rgb_top_down, temp_size);
        if (!resized) {
            set_error(error_text, error_text_size, "allocate RGB24 frame", AVERROR(ENOMEM));
            av_frame_unref(d->frame);
            return TM_ASF_READ_ERROR;
        }
        d->rgb_top_down = resized;
        d->rgb_capacity = temp_size;
    }

    d->scaler = sws_getCachedContext(
        d->scaler,
        d->width, d->height, (enum AVPixelFormat)d->frame->format,
        d->width, d->height, AV_PIX_FMT_BGR24,
        SWS_BILINEAR, NULL, NULL, NULL);
    if (!d->scaler) {
        set_error(error_text, error_text_size, "create RGB24 converter", AVERROR(ENOMEM));
        av_frame_unref(d->frame);
        return TM_ASF_READ_ERROR;
    }

    uint8_t *planes[4] = { d->rgb_top_down, NULL, NULL, NULL };
    int strides[4] = { (int)row_bytes, 0, 0, 0 };
    result = sws_scale(
        d->scaler,
        (const uint8_t * const *)d->frame->data,
        d->frame->linesize,
        0,
        d->height,
        planes,
        strides);
    if (result != d->height) {
        set_error(error_text, error_text_size, "convert RGB24 frame", AVERROR_INVALIDDATA);
        av_frame_unref(d->frame);
        return TM_ASF_READ_ERROR;
    }

    /* DirectShow MEDIASUBTYPE_RGB24 stores RGBTRIPLEs as B,G,R and uses
     * bottom-up rows for a positive-height BI_RGB VIDEOINFOHEADER. */
    for (int y = 0; y < d->height; y++) {
        uint8_t *out_row = dst + (size_t)(d->height - 1 - y) * (size_t)dst_stride;
        memcpy(out_row, d->rgb_top_down + (size_t)y * row_bytes, row_bytes);
        if ((size_t)dst_stride > row_bytes)
            memset(out_row + row_bytes, 0, (size_t)dst_stride - row_bytes);
    }

    if (frame_info) {
        frame_info->pts = d->frame->best_effort_timestamp;
        frame_info->duration = d->frame->duration;
        frame_info->time_base_num = d->time_base.num;
        frame_info->time_base_den = d->time_base.den;
    }

    av_frame_unref(d->frame);
    return TM_ASF_READ_FRAME;
}

int tm_asf_decoder_read_rgb24(
    tm_asf_decoder *d,
    uint8_t *dst,
    size_t dst_capacity,
    int dst_stride,
    tm_asf_frame_info *frame_info,
    char *error_text,
    size_t error_text_size) {
    if (!d || !dst || d->failed) {
        set_error(error_text, error_text_size, "read frame arguments/state", AVERROR(EINVAL));
        return TM_ASF_READ_ERROR;
    }

    for (;;) {
        int result = receive_frame(d, dst, dst_capacity, dst_stride,
                                   frame_info, error_text, error_text_size);
        if (result == TM_ASF_READ_FRAME || result == TM_ASF_READ_EOF)
            return result;
        if (result == TM_ASF_READ_ERROR) {
            d->failed = 1;
            return result;
        }

        if (d->packet_pending) {
            result = avcodec_send_packet(d->codec, d->packet);
            if (result == AVERROR(EAGAIN))
                continue;
            av_packet_unref(d->packet);
            d->packet_pending = 0;
            if (result < 0) {
                set_error(error_text, error_text_size, "submit compressed packet", result);
                d->failed = 1;
                return TM_ASF_READ_ERROR;
            }
            continue;
        }

        if (d->demux_eof) {
            if (!d->flush_sent) {
                result = avcodec_send_packet(d->codec, NULL);
                if (result == AVERROR(EAGAIN))
                    continue;
                if (result < 0 && result != AVERROR_EOF) {
                    set_error(error_text, error_text_size, "flush video decoder", result);
                    d->failed = 1;
                    return TM_ASF_READ_ERROR;
                }
                d->flush_sent = 1;
                continue;
            }
            return TM_ASF_READ_EOF;
        }

        result = av_read_frame(d->format, d->packet);
        if (result == AVERROR_EOF) {
            d->demux_eof = 1;
            continue;
        }
        if (result < 0) {
            set_error(error_text, error_text_size, "read compressed packet", result);
            d->failed = 1;
            return TM_ASF_READ_ERROR;
        }
        if (d->packet->stream_index != d->video_stream) {
            av_packet_unref(d->packet);
            continue;
        }
        d->packet_pending = 1;
    }
}

void tm_asf_decoder_close(tm_asf_decoder *d) {
    if (!d)
        return;
    av_freep(&d->rgb_top_down);
    sws_freeContext(d->scaler);
    av_frame_free(&d->frame);
    av_packet_free(&d->packet);
    avcodec_free_context(&d->codec);
    avformat_close_input(&d->format);
    av_free(d);
}
