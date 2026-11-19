#include "media_internal.h"

#include <libavutil/display.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_WEBP
#include <webp/demux.h>

// A WebP file decoded by libwebp: FFmpeg's decoder cannot do animated WebP
struct WebpFile {
    uint8_t *data;          // the whole file; libwebp reads from it
    size_t size;
    WebPAnimDecoder *dec;
    int prev_ms;            // end time of the last frame handed out
    int frames;
    int64_t total_us;
};
#endif

// Animated images: browsers show delays under 20 ms as 100 ms
#define MIN_DELAY_US 20000
#define DEFAULT_DELAY_US 100000

// Prototype
static void set_av_error(char *err, int err_len, const char *what, int code);
static int is_animated_codec(enum AVCodecID id);
static int pick_lowres(const MediaFile *m, int need_w, int need_h);
static int has_more_frames(MediaFile *m, Decoder *d);
static int orient_from_matrix(const int32_t *m);
static void set_orient(MediaFile *m, int orient);
static Frame *frame_orient(Frame *f, int orient);
#ifdef HAVE_WEBP
static int is_webp_file(const char *path);
static int64_t webp_delay_us(int ms);
static int webp_open(MediaFile *m, const char *path, char *err, int err_len);
static void webp_close(struct WebpFile *w);
static int webp_next(MediaFile *m, AVFrame *out);
#endif

// Write "what: FFmpeg's message" into err
static void set_av_error(char *err, int err_len, const char *what, int code) {
    char msg[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(code, msg, sizeof(msg));
    snprintf(err, (size_t)err_len, "%s: %s", what, msg);
}

static int is_animated_codec(enum AVCodecID id) {
    return id == AV_CODEC_ID_GIF || id == AV_CODEC_ID_APNG ||
           id == AV_CODEC_ID_WEBP;
}

// Largest JPEG-style shrink that still leaves need_w x need_h pixels;
// a JPEG's turn is only known after decoding, so both sides must cover
// the larger need
static int pick_lowres(const MediaFile *m, int need_w, int need_h) {
    int lowres = 0;
    int need = need_w > need_h ? need_w : need_h;
    if (!m->codec || need_w < 1 || need_h < 1) {
        return 0;
    }
    while (lowres < m->codec->max_lowres &&
           (m->coded_w >> (lowres + 1)) >= need &&
           (m->coded_h >> (lowres + 1)) >= need) {
        lowres++;
    }
    return lowres;
}

// EXIF orientation from a display matrix, read the way ffmpeg's autorotate
// does: 1 as stored, 2 mirrored, 3 upside down, 4 flipped, 5 transposed,
// 6 turned clockwise, 7 transversed, 8 turned counterclockwise
static int orient_from_matrix(const int32_t *m) {
    double theta = -round(av_display_rotation_get(m));
    theta -= 360 * floor(theta / 360 + 0.9 / 360);
    if (fabs(theta - 90) < 1) {
        return m[3] > 0 ? 5 : 6;
    }
    if (fabs(theta - 270) < 1) {
        return m[3] < 0 ? 7 : 8;
    }
    if (fabs(theta - 180) < 1) {
        int h = m[0] < 0, v = m[4] < 0;
        return h && v ? 3 : h ? 2 : v ? 4 : 1;
    }
    return m[4] < 0 ? 4 : m[0] < 0 ? 2 : 1;
}

int frame_orient_of(const AVFrame *av, int hint) {
    const AVFrameSideData *sd =
        av_frame_get_side_data(av, AV_FRAME_DATA_DISPLAYMATRIX);
    if (sd && sd->size >= 9 * sizeof(int32_t)) {
        return orient_from_matrix((const int32_t *)sd->data);
    }
    return hint >= 1 && hint <= 8 ? hint : 1;
}

// Record the orientation; turned images swap their displayed size
static void set_orient(MediaFile *m, int orient) {
    m->orient = orient;
    m->width = orient >= 5 ? m->coded_h : m->coded_w;
    m->height = orient >= 5 ? m->coded_w : m->coded_h;
}

// Turn or mirror f as its orientation says; frees f, returns the new frame
static Frame *frame_orient(Frame *f, int orient) {
    if (!f || orient <= 1 || orient > 8) {
        return f;
    }
    int w = f->width, h = f->height;
    int swap = orient >= 5;
    Frame *o = frame_new(swap ? h : w, swap ? w : h);
    if (!o) {
        frame_free(f);
        return NULL;
    }
    for (int y = 0; y < o->height; y++) {
        for (int x = 0; x < o->width; x++) {
            // Which stored pixel lands at x, y
            int sx, sy;
            switch (orient) {
            case 2: sx = w - 1 - x; sy = y; break;
            case 3: sx = w - 1 - x; sy = h - 1 - y; break;
            case 4: sx = x; sy = h - 1 - y; break;
            case 5: sx = y; sy = x; break;
            case 6: sx = y; sy = h - 1 - x; break;
            case 7: sx = w - 1 - y; sy = h - 1 - x; break;
            default: sx = w - 1 - y; sy = x; break;
            }
            memcpy(o->rgb + ((size_t)y * o->width + x) * 3,
                   f->rgb + ((size_t)sy * w + sx) * 3, 3);
        }
    }
    frame_free(f);
    return o;
}

int decoder_open(MediaFile *m, Decoder *d, int lowres, char *err, int err_len) {
    memset(d, 0, sizeof(*d));
    if (m->webp) {
        return decoder_seek(m, d, 0);
    }
    d->ctx = avcodec_alloc_context3(m->codec);
    d->pkt = av_packet_alloc();
    if (!d->ctx || !d->pkt) {
        snprintf(err, (size_t)err_len, "out of memory");
        decoder_close(d);
        return -1;
    }
    avcodec_parameters_to_context(d->ctx, m->fmt->streams[m->stream]->codecpar);
    d->ctx->lowres = lowres;
    d->ctx->thread_count = 0;   // as many as the CPU has
    d->ctx->pkt_timebase = m->fmt->streams[m->stream]->time_base;

    int ret = avcodec_open2(d->ctx, m->codec, NULL);
    if (ret < 0) {
        set_av_error(err, err_len, m->codec->name, ret);
        decoder_close(d);
        return -1;
    }
    return 0;
}

void decoder_close(Decoder *d) {
    avcodec_free_context(&d->ctx);
    av_packet_free(&d->pkt);
}

int decoder_next(MediaFile *m, Decoder *d, AVFrame *out) {
#ifdef HAVE_WEBP
    if (m->webp) {
        return webp_next(m, out);
    }
#endif
    for (;;) {
        int ret = avcodec_receive_frame(d->ctx, out);
        if (ret != AVERROR(EAGAIN)) {
            return ret;     // a frame, the end, or an error
        }
        if (d->draining) {
            return AVERROR_EOF;
        }

        // The decoder wants more input
        ret = av_read_frame(m->fmt, d->pkt);
        if (ret == AVERROR_EOF) {
            // Empty out frames the decoder still holds
            avcodec_send_packet(d->ctx, NULL);
            d->draining = 1;
            continue;
        }
        if (ret < 0) {
            return ret;
        }
        if (d->pkt->stream_index == m->stream) {
            ret = avcodec_send_packet(d->ctx, d->pkt);
            av_packet_unref(d->pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                return ret;
            }
        } else {
            av_packet_unref(d->pkt);
        }
    }
}

int decoder_seek(MediaFile *m, Decoder *d, int64_t t_us) {
#ifdef HAVE_WEBP
    if (m->webp) {
        WebPAnimDecoderReset(m->webp->dec);
        m->webp->prev_ms = 0;
        d->draining = 0;
        d->next_pts_us = 0;
        return 0;
    }
#endif
    AVStream *st = m->fmt->streams[m->stream];
    int animated = is_animated_codec(m->codec->id);
    if (animated) {
        // Animated images restart and decode forward: frames build on each other
        t_us = 0;
    }
    int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    int64_t ts = av_rescale_q(t_us, AV_TIME_BASE_Q, st->time_base) + start;

    int ret = av_seek_frame(m->fmt, m->stream, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0 && t_us == 0) {
        // Some image demuxers only rewind by byte position
        ret = av_seek_frame(m->fmt, m->stream, 0, AVSEEK_FLAG_BYTE);
    }
    avcodec_flush_buffers(d->ctx);
    d->draining = 0;
    d->next_pts_us = animated ? 0 : t_us;
    return ret < 0 ? -1 : 0;
}

void decoder_timing(MediaFile *m, Decoder *d, const AVFrame *av,
                    int64_t *pts_us, int64_t *dur_us) {
#ifdef HAVE_WEBP
    if (m->webp) {
        // webp_next leaves the delay in milliseconds
        *dur_us = webp_delay_us((int)av->duration);
        *pts_us = d->next_pts_us;
        d->next_pts_us += *dur_us;
        return;
    }
#endif
    AVStream *st = m->fmt->streams[m->stream];
    AVRational us = {1, 1000000};
    int64_t dur = av->duration > 0 ? av_rescale_q(av->duration, st->time_base, us)
                                   : 0;

    if (is_animated_codec(m->codec->id)) {
        // Delays add up from the start, whatever the container says
        *dur_us = dur < MIN_DELAY_US ? DEFAULT_DELAY_US : dur;
        *pts_us = d->next_pts_us;
    } else {
        if (dur <= 0) {
            AVRational fr = st->avg_frame_rate;
            dur = fr.num > 0 && fr.den > 0 ? av_rescale(1000000, fr.den, fr.num)
                                           : 40000;
        }
        *dur_us = dur;
        int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
        *pts_us = av->best_effort_timestamp != AV_NOPTS_VALUE
                      ? av_rescale_q(av->best_effort_timestamp - start,
                                     st->time_base, us)
                      : d->next_pts_us;
    }
    d->next_pts_us = *pts_us + *dur_us;
}

Frame *frame_convert(const AVFrame *av, int w, int h, int orient_hint,
                     struct SwsContext **sws, char *err, int err_len) {
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(av->format);
    int alpha = desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
    int orient = frame_orient_of(av, orient_hint);
    if (w < 1 || h < 1) {
        w = av->width;
        h = av->height;
    } else if (orient >= 5) {
        // Scale to the stored shape; turning afterwards gives w x h
        int t = w;
        w = h;
        h = t;
    }

    // An FFmpeg-allocated (padded) output frame: swscale skips the last few
    // pixels of a row when writing into an exact-width buffer
    Frame *f = frame_new(w, h);
    AVFrame *conv = av_frame_alloc();
    if (!*sws) {
        *sws = sws_alloc_context();
        if (*sws) {
            av_opt_set_int(*sws, "sws_flags", SWS_AREA, 0);
        }
    }
    if (!f || !conv || !*sws) {
        snprintf(err, (size_t)err_len, "out of memory");
        goto fail;
    }
    conv->width = w;
    conv->height = h;
    conv->format = alpha ? AV_PIX_FMT_RGBA : AV_PIX_FMT_RGB24;

    // Color range and matrix come from the frame's own metadata, and the
    // context sets itself up again whenever sizes or formats change
    int ret = sws_scale_frame(*sws, conv, av);
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
    av_frame_free(&conv);
    return frame_orient(f, orient);

fail:
    av_frame_free(&conv);
    frame_free(f);
    return NULL;
}

// After a frame was decoded: does the stream go on?
static int has_more_frames(MediaFile *m, Decoder *d) {
#ifdef HAVE_WEBP
    if (m->webp) {
        return WebPAnimDecoderHasMoreFrames(m->webp->dec);
    }
#endif
    if (d->draining) {
        AVFrame *tmp = av_frame_alloc();
        int more = tmp && avcodec_receive_frame(d->ctx, tmp) == 0;
        av_frame_free(&tmp);
        return more;
    }
    int ret;
    while ((ret = av_read_frame(m->fmt, d->pkt)) >= 0) {
        int ours = d->pkt->stream_index == m->stream;
        av_packet_unref(d->pkt);
        if (ours) {
            return 1;
        }
    }
    // Frames still inside the decoder count too (video with delay)
    avcodec_send_packet(d->ctx, NULL);
    AVFrame *tmp = av_frame_alloc();
    int more = tmp && avcodec_receive_frame(d->ctx, tmp) == 0;
    av_frame_free(&tmp);
    return more;
}

#ifdef HAVE_WEBP
// WebP files start with "RIFF", a length, then "WEBP"
static int is_webp_file(const char *path) {
    unsigned char head[12];
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    size_t n = fread(head, 1, sizeof(head), f);
    fclose(f);
    return n == sizeof(head) && memcmp(head, "RIFF", 4) == 0 &&
           memcmp(head + 8, "WEBP", 4) == 0;
}

// Same rule as the other animated formats: under 20 ms means 100 ms
static int64_t webp_delay_us(int ms) {
    int64_t us = (int64_t)ms * 1000;
    return us < MIN_DELAY_US ? DEFAULT_DELAY_US : us;
}

static int webp_open(MediaFile *m, const char *path, char *err, int err_len) {
    struct WebpFile *w = calloc(1, sizeof(*w));
    FILE *f = fopen(path, "rb");
    if (!w || !f) {
        snprintf(err, (size_t)err_len, "%s: cannot open", path);
        goto fail;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    w->data = size > 0 ? malloc((size_t)size) : NULL;
    if (!w->data || fread(w->data, 1, (size_t)size, f) != (size_t)size) {
        snprintf(err, (size_t)err_len, "%s: cannot read", path);
        goto fail;
    }
    w->size = (size_t)size;
    fclose(f);
    f = NULL;

    WebPAnimDecoderOptions opts;
    WebPAnimDecoderOptionsInit(&opts);
    opts.color_mode = MODE_RGBA;
    opts.use_threads = 1;
    WebPData data = {w->data, w->size};
    w->dec = WebPAnimDecoderNew(&data, &opts);
    WebPAnimInfo info;
    if (!w->dec || !WebPAnimDecoderGetInfo(w->dec, &info)) {
        snprintf(err, (size_t)err_len, "%s: not a valid WebP file", path);
        goto fail;
    }
    w->frames = (int)info.frame_count;

    // Add up the delays for the timeline without decoding anything
    const WebPDemuxer *dmux = WebPAnimDecoderGetDemuxer(w->dec);
    WebPIterator it;
    if (WebPDemuxGetFrame(dmux, 1, &it)) {
        do {
            w->total_us += webp_delay_us(it.duration);
        } while (WebPDemuxNextFrame(&it));
        WebPDemuxReleaseIterator(&it);
    }

    m->webp = w;
    m->coded_w = (int)info.canvas_width;
    m->coded_h = (int)info.canvas_height;
    set_orient(m, 1);
    m->kind = MEDIA_STILL;
    return 0;

fail:
    if (f) {
        fclose(f);
    }
    webp_close(w);
    return -1;
}

static void webp_close(struct WebpFile *w) {
    if (w) {
        WebPAnimDecoderDelete(w->dec);
        free(w->data);
        free(w);
    }
}

// Next full canvas as an RGBA AVFrame, so the rest of the pipeline is shared
static int webp_next(MediaFile *m, AVFrame *out) {
    struct WebpFile *w = m->webp;
    if (!WebPAnimDecoderHasMoreFrames(w->dec)) {
        return AVERROR_EOF;
    }
    uint8_t *buf;
    int ts;
    if (!WebPAnimDecoderGetNext(w->dec, &buf, &ts)) {
        return AVERROR_INVALIDDATA;
    }
    av_frame_unref(out);
    out->format = AV_PIX_FMT_RGBA;
    out->width = m->width;
    out->height = m->height;
    if (av_frame_get_buffer(out, 0) < 0) {
        return AVERROR(ENOMEM);
    }
    for (int y = 0; y < m->height; y++) {
        memcpy(out->data[0] + (size_t)y * out->linesize[0],
               buf + (size_t)y * m->width * 4, (size_t)m->width * 4);
    }
    out->duration = ts - w->prev_ms;
    w->prev_ms = ts;
    return 0;
}
#endif

MediaFile *media_open(const char *path, char *err, int err_len) {
    MediaFile *m = calloc(1, sizeof(*m));
    if (!m) {
        snprintf(err, (size_t)err_len, "out of memory");
        return NULL;
    }
    // FFmpeg's own messages would land on top of the viewer; charcoal
    // reports errors itself
    av_log_set_level(AV_LOG_QUIET);

#ifdef HAVE_WEBP
    // libwebp handles every WebP, animated ones included
    if (is_webp_file(path)) {
        if (webp_open(m, path, err, err_len) != 0) {
            goto fail;
        }
        return m;
    }
#endif

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
    m->coded_w = par->width;
    m->coded_h = par->height;
    m->kind = MEDIA_STILL;

    // Phone videos say how to turn them in the stream; JPEG photos only
    // tell on their first frame (see media_decode_at)
    const AVPacketSideData *sd =
        av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data,
                                AV_PKT_DATA_DISPLAYMATRIX);
    set_orient(m, sd && sd->size >= 9 * sizeof(int32_t)
                      ? orient_from_matrix((const int32_t *)sd->data)
                      : 1);
    if (m->width < 1 || m->height < 1) {
        // FFmpeg reads no size when its decoder cannot handle the file,
        // which is what happens to animated WebP before FFmpeg supports it
        snprintf(err, (size_t)err_len,
                 "%s: this FFmpeg build cannot decode it (%s)", path,
                 m->codec->id == AV_CODEC_ID_WEBP
                     ? "animated WebP; build charcoal with libwebp-devel"
                     : m->codec->name);
        goto fail;
    }
    return m;

fail:
    media_close(m);
    return NULL;
}

const char *media_codec_name(const MediaFile *m) {
    if (m->webp) {
        return "webp (libwebp)";
    }
    return m->codec->name;
}

void media_size(const MediaFile *m, int *width, int *height) {
    *width = m->width;
    *height = m->height;
}

MediaKind media_kind(const MediaFile *m) {
    return m->kind;
}

int64_t media_duration_us(const MediaFile *m) {
#ifdef HAVE_WEBP
    if (m->webp) {
        return m->webp->frames > 1 ? m->webp->total_us : 0;
    }
#endif
    AVStream *st = m->fmt->streams[m->stream];
    if (st->duration > 0) {
        return av_rescale_q(st->duration, st->time_base, AV_TIME_BASE_Q);
    }
    return m->fmt->duration > 0 ? m->fmt->duration : 0;
}

double media_fps(const MediaFile *m) {
    if (!m->fmt) {
        return 0.0;
    }
    AVRational fr = m->fmt->streams[m->stream]->avg_frame_rate;
    return fr.num > 0 && fr.den > 0 ? av_q2d(fr) : 0.0;
}

Frame *media_decode_at(MediaFile *m, int64_t t_us, int64_t index, int need_w,
                       int need_h, char *err, int err_len) {
    Decoder d;
    if (decoder_open(m, &d, pick_lowres(m, need_w, need_h), err, err_len) != 0) {
        return NULL;
    }
    AVFrame *av = av_frame_alloc();
    AVFrame *last = av_frame_alloc();
    struct SwsContext *sws = NULL;
    Frame *result = NULL;
    int have = 0;
    if (!av || !last) {
        snprintf(err, (size_t)err_len, "out of memory");
        goto done;
    }
    if (t_us > 0 && index < 0) {
        decoder_seek(m, &d, t_us);
    }

    // Decode forward to the frame on screen at t_us, or the index-th frame
    for (int64_t n = 0;; n++) {
        int ret = decoder_next(m, &d, av);
        if (ret == AVERROR_EOF && have) {
            break;      // past the end: the last frame
        }
        if (ret < 0) {
            set_av_error(err, err_len, "decode", ret);
            goto done;
        }
        int64_t pts, dur;
        decoder_timing(m, &d, av, &pts, &dur);
        av_frame_unref(last);
        av_frame_move_ref(last, av);
        have = 1;
        if (index >= 0 ? n >= index : pts + dur > t_us) {
            break;
        }
    }

    // The first frame may carry the orientation (EXIF); sizes follow it
    set_orient(m, frame_orient_of(last, m->orient));
    result = frame_convert(last, 0, 0, m->orient, &sws, err, err_len);
    if (result && t_us == 0 && index <= 0) {
        // The first frame tells still images apart from animations
        if (has_more_frames(m, &d)) {
            m->kind = m->webp || is_animated_codec(m->codec->id)
                          ? MEDIA_ANIMATED
                          : MEDIA_VIDEO;
        }
    }

done:
    sws_free_context(&sws);
    av_frame_free(&av);
    av_frame_free(&last);
    decoder_close(&d);
    return result;
}

Frame *media_decode_first(MediaFile *m, int need_w, int need_h, char *err,
                          int err_len) {
    return media_decode_at(m, 0, 0, need_w, need_h, err, err_len);
}

void media_versions(char *out, int n) {
    unsigned v = avformat_version();
    int len = snprintf(out, (size_t)n, "libavformat %u.%u.%u", v >> 16,
                       (v >> 8) & 0xFF, v & 0xFF);
#ifdef HAVE_WEBP
    int w = WebPGetDemuxVersion();
    if (len > 0 && len < n) {
        snprintf(out + len, (size_t)(n - len), ", libwebp %d.%d.%d", w >> 16,
                 (w >> 8) & 0xFF, w & 0xFF);
    }
#else
    if (len > 0 && len < n) {
        snprintf(out + len, (size_t)(n - len), ", no libwebp");
    }
#endif
}

void media_close(MediaFile *m) {
    if (m) {
#ifdef HAVE_WEBP
        webp_close(m->webp);
#endif
        avformat_close_input(&m->fmt);
        free(m);
    }
}
