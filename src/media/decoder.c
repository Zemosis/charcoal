#include "media.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// An opened file, the codec is opened per decode so lowres can be chosen
struct MediaFile {
    AVFormatContext *fmt;
    const AVCodec *codec;
    int stream;
    int width;
    int height;
};

// Prototype
static void set_av_error(char *err, int err_len, const char *what, int code);
static Frame *frame_from_av(const AVFrame *av, char *err, int err_len);
static int decode_first(AVFormatContext *fmt, AVCodecContext *dec, int stream,
                        AVFrame *out);

// Write "what: FFmpeg's message" into err
static void set_av_error(char *err, int err_len, const char *what, int code) {
    char msg[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(code, msg, sizeof(msg));
    snprintf(err, (size_t)err_len, "%s: %s", what, msg);
}

// Convert any decoded frame to RGB24, transparent pixels over black
// sws_scale_frame allocates a padded output frame: swscale skips the last
// few pixels of a row when writing into an exact-width buffer
static Frame *frame_from_av(const AVFrame *av, char *err, int err_len) {
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(av->format);
    int alpha = desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
    int w = av->width;
    int h = av->height;

    Frame *f = frame_new(w, h);
    AVFrame *conv = av_frame_alloc();
    struct SwsContext *sws = sws_alloc_context();
    if (!f || !conv || !sws) {
        snprintf(err, (size_t)err_len, "out of memory");
        goto fail;
    }
    conv->width = w;
    conv->height = h;
    conv->format = alpha ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24;

    // Color range and matrix come from the frame's own metadata
    int ret = sws_scale_frame(sws, conv, av);
    if (ret < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, msg, sizeof(msg));
        snprintf(err, (size_t)err_len, "cannot convert pixel format %s: %s",
                 av_get_pix_fmt_name(av->format), msg);
        goto fail;
    }

    for (int y = 0; y < h; y++) {
        const uint8_t *src = conv->data[0] + (size_t)y * conv->linesize[0];
        uint8_t *dst = f->rgb + (size_t)y * w * 3;
        if (!alpha) {
            memcpy(dst, src, (size_t)w * 3);
            continue;
        }
        for (int x = 0; x < w; x++, src += 4, dst += 3) {
            unsigned a = src[3];
            dst[0] = (uint8_t)(src[0] * a / 255);
            dst[1] = (uint8_t)(src[1] * a / 255);
            dst[2] = (uint8_t)(src[2] * a / 255);
        }
    }
    sws_free_context(&sws);
    av_frame_free(&conv);
    return f;

fail:
    sws_free_context(&sws);
    av_frame_free(&conv);
    frame_free(f);
    return NULL;
}

// Read packets until the decoder gives back one frame, returns 0 or an AVERROR
static int decode_first(AVFormatContext *fmt, AVCodecContext *dec, int stream,
                        AVFrame *out) {
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        return AVERROR(ENOMEM);
    }

    int ret;
    while ((ret = av_read_frame(fmt, pkt)) >= 0) {
        if (pkt->stream_index == stream) {
            ret = avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
            if (ret < 0) {
                break;
            }
            ret = avcodec_receive_frame(dec, out);
            if (ret != AVERROR(EAGAIN)) {
                break;
            }
        } else {
            av_packet_unref(pkt);
        }
    }

    // End of file: flush frames still held inside the decoder
    if (ret == AVERROR_EOF) {
        avcodec_send_packet(dec, NULL);
        ret = avcodec_receive_frame(dec, out);
    }
    av_packet_free(&pkt);
    return ret;
}

MediaFile *media_open(const char *path, char *err, int err_len) {
    MediaFile *m = calloc(1, sizeof(*m));
    if (!m) {
        snprintf(err, (size_t)err_len, "out of memory");
        return NULL;
    }
    av_log_set_level(AV_LOG_ERROR);

    int ret = avformat_open_input(&m->fmt, path, NULL, NULL);
    if (ret < 0) {
        set_av_error(err, err_len, path, ret);
        goto fail;
    }
    if ((ret = avformat_find_stream_info(m->fmt, NULL)) < 0) {
        set_av_error(err, err_len, path, ret);
        goto fail;
    }
    m->stream = av_find_best_stream(m->fmt, AVMEDIA_TYPE_VIDEO, -1, -1,
                                    &m->codec, 0);
    if (m->stream < 0 || !m->codec) {
        snprintf(err, (size_t)err_len, "%s: no image or video stream", path);
        goto fail;
    }

    AVCodecParameters *par = m->fmt->streams[m->stream]->codecpar;
    m->width = par->width;
    m->height = par->height;
    if (m->width < 1 || m->height < 1) {
        snprintf(err, (size_t)err_len, "%s: unknown image size", path);
        goto fail;
    }
    return m;

fail:
    media_close(m);
    return NULL;
}

void media_size(const MediaFile *m, int *width, int *height) {
    *width = m->width;
    *height = m->height;
}

Frame *media_decode_first(MediaFile *m, int need_w, int need_h, char *err,
                          int err_len) {
    AVCodecContext *dec = avcodec_alloc_context3(m->codec);
    AVFrame *av = av_frame_alloc();
    Frame *result = NULL;
    if (!dec || !av) {
        snprintf(err, (size_t)err_len, "out of memory");
        goto done;
    }
    avcodec_parameters_to_context(dec, m->fmt->streams[m->stream]->codecpar);

    // Let decoders that can (JPEG) shrink while decoding, keeping enough pixels
    int lowres = 0;
    while (lowres < m->codec->max_lowres && need_w > 0 && need_h > 0 &&
           (m->width >> (lowres + 1)) >= need_w &&
           (m->height >> (lowres + 1)) >= need_h) {
        lowres++;
    }
    dec->lowres = lowres;

    int ret = avcodec_open2(dec, m->codec, NULL);
    if (ret < 0) {
        set_av_error(err, err_len, m->codec->name, ret);
        goto done;
    }
    if ((ret = decode_first(m->fmt, dec, m->stream, av)) < 0) {
        set_av_error(err, err_len, "decode", ret);
        goto done;
    }
    result = frame_from_av(av, err, err_len);

done:
    av_frame_free(&av);
    avcodec_free_context(&dec);
    return result;
}

void media_close(MediaFile *m) {
    if (m) {
        avformat_close_input(&m->fmt);
        free(m);
    }
}
