#ifndef TM_ASF_DECODER_H
#define TM_ASF_DECODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tm_asf_decoder tm_asf_decoder;

typedef struct tm_asf_video_info {
    int width;
    int height;
    int codec_id;
    /* Stream duration in time_base ticks, or AV_NOPTS_VALUE if unknown. */
    int64_t duration;
    int64_t time_base_num;
    int64_t time_base_den;
    int64_t frame_rate_num;
    int64_t frame_rate_den;
    char codec_name[32];
} tm_asf_video_info;

typedef struct tm_asf_frame_info {
    int64_t pts;
    int64_t duration;
    int64_t time_base_num;
    int64_t time_base_den;
} tm_asf_frame_info;

enum tm_asf_read_result {
    TM_ASF_READ_ERROR = -1,
    TM_ASF_READ_EOF = 0,
    TM_ASF_READ_FRAME = 1
};

/*
 * Open an ASF file and select its best decodable video stream.
 *
 * The returned decoder owns all FFmpeg contexts. On failure, *out is NULL
 * and error_text (when supplied) receives a human-readable diagnostic.
 */
int tm_asf_decoder_open(
    const char *path,
    tm_asf_decoder **out,
    tm_asf_video_info *info,
    char *error_text,
    size_t error_text_size);

/*
 * Decode one frame and convert it to bottom-up DirectShow RGB24, suitable
 * for a positive-height BI_RGB VIDEOINFOHEADER. RGBTRIPLE bytes are B,G,R.
 * dst_stride must be >= width * 3;
 * dst_capacity must cover dst_stride * height bytes.
 *
 * Returns TM_ASF_READ_FRAME, TM_ASF_READ_EOF, or TM_ASF_READ_ERROR.
 */
int tm_asf_decoder_read_rgb24(
    tm_asf_decoder *decoder,
    uint8_t *dst,
    size_t dst_capacity,
    int dst_stride,
    tm_asf_frame_info *frame_info,
    char *error_text,
    size_t error_text_size);

void tm_asf_decoder_close(tm_asf_decoder *decoder);

#ifdef __cplusplus
}
#endif

#endif
