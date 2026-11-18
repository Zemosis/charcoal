#ifndef CHARCOAL_MEDIA_H
#define CHARCOAL_MEDIA_H

#include <stdint.h>

// One decoded RGB24 image
typedef struct Frame {
    int width;
    int height;
    uint8_t *rgb;       // width * height * 3
    int64_t pts_us;     // presentation time, microseconds
} Frame;

// What kind of input a file turned out to be
typedef enum MediaKind {
    MEDIA_STILL,
    MEDIA_ANIMATED,
    MEDIA_VIDEO
} MediaKind;

// Allocate a frame with uninitialized pixels, NULL on failure
Frame *frame_new(int width, int height);

// Free a frame and its pixels
void frame_free(Frame *f);

// An opened media file (FFmpeg state stays inside decoder.c)
typedef struct MediaFile MediaFile;

// Open any FFmpeg-readable file and find its video stream
// On failure returns NULL and writes a message to err
MediaFile *media_open(const char *path, char *err, int err_len);

// Full-resolution size of the image or video
void media_size(const MediaFile *m, int *width, int *height);

// Decode the first frame, at least need_w x need_h pixels when the codec can
// decode smaller (0 means full size), NULL on failure with a message in err
Frame *media_decode_first(MediaFile *m, int need_w, int need_h, char *err,
                          int err_len);

// Close the file, NULL is allowed
void media_close(MediaFile *m);

#endif // CHARCOAL_MEDIA_H
