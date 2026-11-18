#ifndef CHARCOAL_MEDIA_INTERNAL_H
#define CHARCOAL_MEDIA_INTERNAL_H

// Shared by decoder.c and player.c; the rest of charcoal never sees FFmpeg

#include "media.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

// WebP files read with libwebp (defined in decoder.c when built with it)
struct WebpFile;

// An opened file: through FFmpeg, or through libwebp when webp is set
struct MediaFile {
    AVFormatContext *fmt;
    const AVCodec *codec;
    int stream;
    int width;
    int height;
    MediaKind kind;     // known after the first frame is decoded
    struct WebpFile *webp;
};

// A codec opened on a file's video stream, plus decode state
typedef struct Decoder {
    AVCodecContext *ctx;
    AVPacket *pkt;
    int draining;           // end of file reached, emptying the decoder
    int64_t next_pts_us;    // where the next frame starts, for timing fixes
} Decoder;

// Open the codec; lowres shrinks JPEGs while decoding, 0 is full size
int decoder_open(MediaFile *m, Decoder *d, int lowres, char *err, int err_len);

void decoder_close(Decoder *d);

// Decode the next frame into out, returns 0, AVERROR_EOF or another error
int decoder_next(MediaFile *m, Decoder *d, AVFrame *out);

// Jump to the keyframe at or before t_us (0 = start), returns 0 on success
int decoder_seek(MediaFile *m, Decoder *d, int64_t t_us);

// Start time and duration of a decoded frame in microseconds; animated
// images get the browser rule (delays under 20 ms become 100 ms) and
// timestamps that simply add up the delays
void decoder_timing(MediaFile *m, Decoder *d, const AVFrame *av,
                    int64_t *pts_us, int64_t *dur_us);

// Convert to RGB24 at w x h (area-averaged), transparent pixels over black
// sws is reused between calls and freed by the caller with sws_free_context
Frame *frame_convert(const AVFrame *av, int w, int h, struct SwsContext **sws,
                     char *err, int err_len);

#endif // CHARCOAL_MEDIA_INTERNAL_H
