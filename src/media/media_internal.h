#ifndef CHARCOAL_MEDIA_INTERNAL_H
#define CHARCOAL_MEDIA_INTERNAL_H

// Shared by decoder.c and player.c; the rest of charcoal never sees FFmpeg

#include "media.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

// FFmpeg 4.4 to 8 are supported; newer APIs fall back on older systems

// AVFrame.duration is FFmpeg 6.0; before, pkt_duration
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 30, 100)
#define FRAME_DURATION(f) ((f)->duration)
#else
#define FRAME_DURATION(f) ((f)->pkt_duration)
#endif

// Stream side data moved into AVCodecParameters in FFmpeg 6.1
#define HAVE_CODED_SIDE_DATA \
    (LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 29, 100))

// FFmpeg 8 scales frames with a context that configures itself; before,
// the context is set up for each source size and format
#ifndef HAVE_SWS_AUTO
#define HAVE_SWS_AUTO (LIBSWSCALE_VERSION_MAJOR >= 9)
#endif

// Free a scaling context and clear the pointer
#define SWS_FREE(ctx) \
    do { \
        sws_freeContext(ctx); \
        (ctx) = NULL; \
    } while (0)

// WebP files read with libwebp (defined in decoder.c when built with it)
struct WebpFile;

// An opened file: through FFmpeg, or through libwebp when webp is set
struct MediaFile {
    AVFormatContext *fmt;
    const AVCodec *codec;
    int stream;
    int width;          // as displayed: swapped when turned 90 degrees
    int height;
    int coded_w;        // as stored in the file
    int coded_h;
    int orient;         // EXIF-style orientation 1-8 (1 = as stored)
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

// Orientation 1-8 of a decoded frame: its own display matrix (JPEG EXIF
// arrives this way), else hint (from the stream), else 1
int frame_orient_of(const AVFrame *av, int hint);

// Convert to RGB24 at w x h as displayed (area-averaged), turned and
// mirrored per its orientation, transparent pixels over black; 0 x 0 keeps
// the decoded size. sws is reused between calls; free with sws_free_context
Frame *frame_convert(const AVFrame *av, int w, int h, int orient_hint,
                     struct SwsContext **sws, char *err, int err_len);

#endif // CHARCOAL_MEDIA_INTERNAL_H
